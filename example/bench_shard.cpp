// 分片压测：对比 1 / 2 / 4 片（库内 ShardedEffectiveSink）的吞吐。
// 「改动前」= 1 片；「改动后」= 2 / 4 片，路由用轮询以便负载均衡。
// 公平 e2e：pending 上限拉高，避免 P-drop-low 把数字测虚。

#include "log_common.h"
#include "log_handle.h"
#include "sharded_effective_sink.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr const char* kPubKey =
    "04827405069030E26A211C973C8710E6FBE79B5CAA364AC111FB171311902277537F8852EADD17EB339EB7CD0BA2490A58CDED2C702DFC1E"
    "FC7EDB544B869F039C";

// 按片数构造分片 Sink；压测用轮询，并关闭丢弃干扰
std::shared_ptr<logger::ShardedEffectiveSink> MakeSharded(int shards, const std::filesystem::path& root) {
    logger::ShardedEffectiveSink::Conf conf;
    conf.shard_count = shards;
    conf.policy = logger::ShardedEffectiveSink::RoutePolicy::kRoundRobin;
    conf.base.dir = root;
    conf.base.prefix = "bench";
    conf.base.pub_key = kPubKey;
    conf.base.single_size = logger::megabytes(64);
    conf.base.total_size = logger::megabytes(256);
    conf.base.cache_size = logger::megabytes(4);
    // 把队列上限抬高：测的是吞吐上限，不是丢日志策略
    conf.base.max_pending_tasks = 5000000;
    conf.base.max_pending_bytes = size_t{1} << 30;
    conf.base.drop_below = logger::LogLevel::kTrace;
    conf.base.cache_write_retries = 3;
    std::filesystem::create_directories(root);
    return std::make_shared<logger::ShardedEffectiveSink>(std::move(conf));
}

struct Result {
    int shards;
    size_t msg_len;
    long e2e_rate;    // 写完 + Flush 的稳态吞吐（条/秒）
    long burst_rate;  // 仅入队、尚未等 Flush 的突发吞吐
    uint64_t drop_q;
    uint64_t drop_c;
};

Result BenchShards(int shards, size_t msg_len, size_t n, int producers, const std::filesystem::path& root) {
    auto dir = root / ("n" + std::to_string(shards) + "_m" + std::to_string(msg_len));
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    auto sharded = MakeSharded(shards, dir);
    logger::LogHandle handle({sharded});
    logger::SourceLocation loc{};
    std::string msg(msg_len, 'x');

    // 预热：避开冷启动（建文件、ECDH 等）对计时的干扰
    for (size_t i = 0; i < 2000; ++i) {
        handle.Log(logger::LogLevel::kInfo, loc, msg);
    }
    sharded->Flush();

    // ---------- 突发：只计入队时间 ----------
    auto b0 = std::chrono::steady_clock::now();
    if (producers <= 1) {
        for (size_t i = 0; i < n; ++i) {
            handle.Log(logger::LogLevel::kInfo, loc, msg);
        }
    } else {
        std::vector<std::thread> threads;
        const size_t per = n / static_cast<size_t>(producers);
        for (int p = 0; p < producers; ++p) {
            threads.emplace_back([&, p] {
                const size_t begin = static_cast<size_t>(p) * per;
                const size_t end = (p + 1 == producers) ? n : begin + per;
                for (size_t i = begin; i < end; ++i) {
                    handle.Log(logger::LogLevel::kInfo, loc, msg);
                }
            });
        }
        for (auto& t : threads) {
            t.join();
        }
    }
    auto b1 = std::chrono::steady_clock::now();
    sharded->Flush();

    auto st0 = sharded->GetStats();

    // ---------- 端到端：入队 + 全部 Flush 落盘 ----------
    auto t0 = std::chrono::steady_clock::now();
    if (producers <= 1) {
        for (size_t i = 0; i < n; ++i) {
            handle.Log(logger::LogLevel::kInfo, loc, msg);
        }
    } else {
        std::vector<std::thread> threads;
        const size_t per = n / static_cast<size_t>(producers);
        for (int p = 0; p < producers; ++p) {
            threads.emplace_back([&, p] {
                const size_t begin = static_cast<size_t>(p) * per;
                const size_t end = (p + 1 == producers) ? n : begin + per;
                for (size_t i = begin; i < end; ++i) {
                    handle.Log(logger::LogLevel::kInfo, loc, msg);
                }
            });
        }
        for (auto& t : threads) {
            t.join();
        }
    }
    sharded->Flush();
    auto t1 = std::chrono::steady_clock::now();

    auto st1 = sharded->GetStats();
    const double burst_s = std::chrono::duration<double>(b1 - b0).count();
    const double e2e_s = std::chrono::duration<double>(t1 - t0).count();

    Result r;
    r.shards = shards;
    r.msg_len = msg_len;
    r.burst_rate = static_cast<long>(n / burst_s);
    r.e2e_rate = static_cast<long>(n / e2e_s);
    r.drop_q = st1.dropped_by_queue - st0.dropped_by_queue;
    r.drop_c = st1.dropped_by_cache - st0.dropped_by_cache;
    return r;
}

void PrintResult(const Result& r, const Result* baseline) {
    std::cout << "shards=" << r.shards << " msg=" << r.msg_len << " e2e=" << r.e2e_rate << "/s"
              << " burst=" << r.burst_rate << "/s"
              << " drop_q=" << r.drop_q << " drop_c=" << r.drop_c;
    if (baseline && baseline->e2e_rate > 0) {
        const double speedup = static_cast<double>(r.e2e_rate) / static_cast<double>(baseline->e2e_rate);
        std::cout << " e2e相对1片加速=" << speedup << "x";
    }
    std::cout << "\n";
}

}  // namespace

int main() {
    const auto root = std::filesystem::path("/tmp/mylogger_bench_shard");
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    std::cout << "=== 库内 ShardedEffectiveSink 压测（1片=改动前，2/4片=改动后）===\n";
    std::cout << "路由：轮询。pending 拉高，保证不因丢日志虚高吞吐。\n\n";

    struct Case {
        size_t msg_len;
        size_t n;
        int producers;
        const char* title;
    };
    const Case cases[] = {
        {64, 80000, 1, "64B 单生产者"},
        {64, 120000, 4, "64B 四生产者"},
        {4096, 30000, 1, "4KB 单生产者"},
        {4096, 40000, 4, "4KB 四生产者"},
    };

    for (const auto& c : cases) {
        std::cout << "--- " << c.title << " n=" << c.n << " ---\n";
        Result base{};
        bool have_base = false;
        for (int shards : {1, 2, 4}) {
            auto r = BenchShards(shards, c.msg_len, c.n, c.producers, root);
            if (!have_base) {
                base = r;
                have_base = true;
                PrintResult(r, nullptr);
            } else {
                PrintResult(r, &base);
            }
        }
        std::cout << "\n";
    }

    std::filesystem::remove_all(root);
    std::cout << "完成，已清理 " << root << "\n";
    return 0;
}
