#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "effective_sink.h"
#include "sink.h"

namespace logger {

// 分片门面 Sink：内部持有 N 条独立流水线（每条 = 一个 EffectiveSink
// = 自己的队列 + 单消费者 TaskRunner + 自己的 mmap/文件）。
// 片与片之间可并行消费；片内仍保序；片间不保证全局顺序。
class ShardedEffectiveSink final : public LogSink {
public:
    // 选片策略：决定「这条日志进哪条流水线」
    enum class RoutePolicy {
        // 轮询：第 0、1、2… 条依次进片，负载最均匀（压测测上限常用）
        kRoundRobin,
        // 按源文件名哈希：同一 .cpp 打出的日志固定进同一片（近似「按模块」）
        kByFileName,
        // 按模块名：先 BindModule，再 SetThreadModule；未绑定则退回轮询
        kByModule,
    };

    struct Conf {
        // 各片共用的 EffectiveSink 配置模板（公钥、容量、背压等）
        // 实际每片会落到 dir/shard_i/，文件前缀变成 prefix_s{i}
        EffectiveSink::Conf base;
        int shard_count{2};                              // 片数 N（建议 ≤ CPU 核数）
        RoutePolicy policy{RoutePolicy::kByModule};      // 默认按模块分流（对齐「分模块多实例」）
    };

    explicit ShardedEffectiveSink(Conf conf);

    ~ShardedEffectiveSink() override = default;

    // 把逻辑模块名固定绑到某一片（仅 kByModule 使用）；shard_index ∈ [0, N)
    void BindModule(const std::string& module, int shard_index);

    // 当前线程的「模块粘性」：打日志前设好，Log 时按模块选片
    // （LogMsg 本身没有模块字段，用线程局部变量补）
    static void SetThreadModule(std::string module);
    static void ClearThreadModule();  // 清空本线程模块名

    void Log(const LogMsg& msg) override;

    void SetFormatter(std::unique_ptr<Formatter> formatter) override;

    void Flush() override;  // 刷空所有片

    int shard_count() const { return static_cast<int>(shards_.size()); }

    // 汇总各片背压/丢弃统计
    EffectiveSink::Stats GetStats() const;

private:
    // 按 conf_.policy 选出本条日志应进入的片下标
    int SelectShard_(const LogMsg& msg);

    Conf conf_;
    std::vector<std::shared_ptr<EffectiveSink>> shards_;  // N 条独立流水线
    std::atomic<uint64_t> rr_{0};                         // 轮询计数器

    mutable std::mutex module_mu_;
    std::unordered_map<std::string, int> module_to_shard_;  // 模块名 → 片下标
};

}  // namespace logger
