#include "sharded_effective_sink.h"

#include <filesystem>
#include <functional>

#include "internal_log.h"

namespace logger {
namespace {

// 本线程当前绑定的模块名（配合 kByModule + SetThreadModule）
thread_local std::string tls_module;

size_t HashFileName(StringView file_name) {
    return std::hash<std::string_view>{}(file_name);
}

}  // namespace

ShardedEffectiveSink::ShardedEffectiveSink(Conf conf) : conf_(std::move(conf)) {
    if (conf_.shard_count < 1) {
        conf_.shard_count = 1;
    }
    shards_.reserve(static_cast<size_t>(conf_.shard_count));
    // 为每一片建独立目录与 EffectiveSink，避免抢同一份 master/slave.cache
    for (int i = 0; i < conf_.shard_count; ++i) {
        EffectiveSink::Conf c = conf_.base;
        const auto shard_dir = conf_.base.dir / ("shard_" + std::to_string(i));
        std::filesystem::create_directories(shard_dir);
        c.dir = shard_dir;
        c.prefix = conf_.base.prefix.empty() ? ("s" + std::to_string(i))
                                             : (conf_.base.prefix + "_s" + std::to_string(i));
        shards_.push_back(std::make_shared<EffectiveSink>(std::move(c)));
    }
    LOG_INFO("ShardedEffectiveSink: shard_count={}, policy={}", conf_.shard_count,
             static_cast<int>(conf_.policy));
}

void ShardedEffectiveSink::BindModule(const std::string& module, int shard_index) {
    if (shard_index < 0 || shard_index >= static_cast<int>(shards_.size())) {
        LOG_ERROR("ShardedEffectiveSink::BindModule: invalid shard_index={} for module={}", shard_index,
                  module);
        return;
    }
    std::lock_guard<std::mutex> lock(module_mu_);
    module_to_shard_[module] = shard_index;
}

void ShardedEffectiveSink::SetThreadModule(std::string module) {
    tls_module = std::move(module);
}

void ShardedEffectiveSink::ClearThreadModule() {
    tls_module.clear();
}

int ShardedEffectiveSink::SelectShard_(const LogMsg& msg) {
    const int n = static_cast<int>(shards_.size());
    switch (conf_.policy) {
        case RoutePolicy::kRoundRobin:
            // 原子自增取模，多生产者下负载也较均匀
            return static_cast<int>(rr_.fetch_add(1, std::memory_order_relaxed) % static_cast<uint64_t>(n));
        case RoutePolicy::kByFileName:
            // 同一源文件 → 同一片，片内有序；不同文件可并行
            return static_cast<int>(HashFileName(msg.location.file_name) % static_cast<size_t>(n));
        case RoutePolicy::kByModule: {
            if (!tls_module.empty()) {
                std::lock_guard<std::mutex> lock(module_mu_);
                auto it = module_to_shard_.find(tls_module);
                if (it != module_to_shard_.end()) {
                    return it->second;
                }
            }
            // 未 Bind / 未 SetThreadModule：退回轮询，避免全挤进第 0 片
            return static_cast<int>(rr_.fetch_add(1, std::memory_order_relaxed) % static_cast<uint64_t>(n));
        }
    }
    return 0;
}

void ShardedEffectiveSink::Log(const LogMsg& msg) {
    const int idx = SelectShard_(msg);
    shards_[static_cast<size_t>(idx)]->Log(msg);
}

void ShardedEffectiveSink::SetFormatter(std::unique_ptr<Formatter>) {
    // 门面不设统一 Formatter；各片 EffectiveSink 构造时自带自己的 formatter
}

void ShardedEffectiveSink::Flush() {
    for (auto& s : shards_) {
        s->Flush();
    }
}

EffectiveSink::Stats ShardedEffectiveSink::GetStats() const {
    EffectiveSink::Stats total{};
    for (auto& s : shards_) {
        auto st = s->GetStats();
        total.dropped_by_queue += st.dropped_by_queue;
        total.dropped_by_cache += st.dropped_by_cache;
        total.cache_write_retries += st.cache_write_retries;
        total.pending_tasks += st.pending_tasks;
        total.pending_bytes += st.pending_bytes;
    }
    return total;
}

}  // namespace logger
