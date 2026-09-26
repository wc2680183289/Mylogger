// Mylogger 分片 vs spdlog 异步写文件：直观对照表
// 指标：端到端（写完 + Flush）条/秒；pending 拉高，避免丢日志虚高。
// 注意：Mylogger 路径含 Proto+Zstd+AES；spdlog 为明文写盘，绝对值通常更快，
//       重点看「分片相对 1 片」的提升，以及与 spdlog 的量级差距。

#include "log_common.h"
#include "log_handle.h"
#include "sharded_effective_sink.h"

#include <spdlog/async.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr const char* kPubKey =
    "04827405069030E26A211C973C8710E6FBE79B5CAA364AC111FB171311902277537F8852EADD17EB339EB7CD0BA2490A58CDED2C702DFC1E"
    "FC7EDB544B869F039C";

struct Row {
    std::string name;
    long e2e_rate{0};
    long burst_rate{0};
};

void RunProducers(int producers, size_t n, const std::function<void(size_t begin, size_t end)>& fn) {
    if (producers <= 1) {
        fn(0, n);
        return;
    }
    std::vector<std::thread> threads;
    const size_t per = n / static_cast<size_t>(producers);
    for (int p = 0; p < producers; ++p) {
        threads.emplace_back([&, p] {
            const size_t begin = static_cast<size_t>(p) * per;
            const size_t end = (p + 1 == producers) ? n : begin + per;
            fn(begin, end);
        });
    }
    for (auto& t : threads) {
        t.join();
    }
}

Row BenchMylogger(int shards, size_t msg_len, size_t n, int producers, const std::filesystem::path& root) {
    auto dir = root / ("my_s" + std::to_string(shards) + "_m" + std::to_string(msg_len) + "_p" +
                       std::to_string(producers));
    std::filesystem::remove_all(dir);

    logger::ShardedEffectiveSink::Conf conf;
    conf.shard_count = shards;
    conf.policy = logger::ShardedEffectiveSink::RoutePolicy::kRoundRobin;
    conf.base.dir = dir;
    conf.base.prefix = "bench";
    conf.base.pub_key = kPubKey;
    conf.base.single_size = logger::megabytes(64);
    conf.base.total_size = logger::megabytes(256);
    conf.base.cache_size = logger::megabytes(4);
    conf.base.max_pending_tasks = 5000000;
    conf.base.max_pending_bytes = size_t{1} << 30;
    conf.base.drop_below = logger::LogLevel::kTrace;
    conf.base.cache_write_retries = 3;

    auto sink = std::make_shared<logger::ShardedEffectiveSink>(std::move(conf));
    logger::LogHandle handle({sink});
    logger::SourceLocation loc{};
    std::string msg(msg_len, 'x');

    for (size_t i = 0; i < 2000; ++i) {
        handle.Log(logger::LogLevel::kInfo, loc, msg);
    }
    sink->Flush();

    auto b0 = std::chrono::steady_clock::now();
    RunProducers(producers, n, [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            handle.Log(logger::LogLevel::kInfo, loc, msg);
        }
    });
    auto b1 = std::chrono::steady_clock::now();
    sink->Flush();

    auto t0 = std::chrono::steady_clock::now();
    RunProducers(producers, n, [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            handle.Log(logger::LogLevel::kInfo, loc, msg);
        }
    });
    sink->Flush();
    auto t1 = std::chrono::steady_clock::now();

    Row r;
    r.name = "Mylogger-" + std::to_string(shards) + "片";
    r.burst_rate = static_cast<long>(n / std::chrono::duration<double>(b1 - b0).count());
    r.e2e_rate = static_cast<long>(n / std::chrono::duration<double>(t1 - t0).count());
    return r;
}

Row BenchSpdlog(int workers, size_t msg_len, size_t n, int producers, const std::filesystem::path& root) {
    auto dir = root / ("spd_w" + std::to_string(workers) + "_m" + std::to_string(msg_len) + "_p" +
                       std::to_string(producers));
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto path = (dir / "spd.log").string();

    // 每个场景独立线程池，避免互相干扰
    spdlog::shutdown();
    spdlog::init_thread_pool(1 << 20, workers);
    auto logger = spdlog::create_async<spdlog::sinks::basic_file_sink_mt>("spd_cmp", path);
    logger->set_pattern("%v");
    logger->set_level(spdlog::level::info);

    std::string msg(msg_len, 'x');
    for (size_t i = 0; i < 2000; ++i) {
        logger->info("{}", msg);
    }
    logger->flush();

    auto b0 = std::chrono::steady_clock::now();
    RunProducers(producers, n, [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            logger->info("{}", msg);
        }
    });
    auto b1 = std::chrono::steady_clock::now();
    logger->flush();

    auto t0 = std::chrono::steady_clock::now();
    RunProducers(producers, n, [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            logger->info("{}", msg);
        }
    });
    logger->flush();
    auto t1 = std::chrono::steady_clock::now();

    Row r;
    r.name = "spdlog异步-" + std::to_string(workers) + "线程";
    r.burst_rate = static_cast<long>(n / std::chrono::duration<double>(b1 - b0).count());
    r.e2e_rate = static_cast<long>(n / std::chrono::duration<double>(t1 - t0).count());

    spdlog::drop("spd_cmp");
    spdlog::shutdown();
    return r;
}

// metric: "e2e" 用 e2e_rate；"burst" 用 burst_rate（纯入队，计时区内不 Flush）
void PrintTable(const std::string& title, const std::vector<Row>& rows, const char* metric) {
    const bool use_burst = (std::string(metric) == "burst");
    std::cout << "\n【" << title << " · " << (use_burst ? "纯入队(不计Flush)" : "e2e+Flush") << "】\n";
    std::cout << std::left << std::setw(22) << "实现" << std::right << std::setw(14) << "条/秒"
              << std::setw(12) << "相对1片" << std::setw(14) << "相对spdlog\n";
    std::cout << std::string(62, '-') << "\n";

    auto pick = [use_burst](const Row& r) { return use_burst ? r.burst_rate : r.e2e_rate; };

    long base_my = 0;
    long base_spd = 0;
    for (const auto& r : rows) {
        if (r.name.find("Mylogger-1片") != std::string::npos) {
            base_my = pick(r);
        }
        if (r.name.find("spdlog异步-1") != std::string::npos) {
            base_spd = pick(r);
        }
    }
    if (base_spd == 0) {
        for (const auto& r : rows) {
            if (r.name.find("spdlog") != std::string::npos) {
                base_spd = pick(r);
                break;
            }
        }
    }

    for (const auto& r : rows) {
        const long rate = pick(r);
        std::cout << std::left << std::setw(22) << r.name << std::right << std::setw(14) << rate;
        if (base_my > 0 && r.name.find("Mylogger") != std::string::npos) {
            std::cout << std::setw(11) << std::fixed << std::setprecision(2)
                      << (static_cast<double>(rate) / static_cast<double>(base_my)) << "x";
        } else {
            std::cout << std::setw(12) << "-";
        }
        if (base_spd > 0) {
            std::cout << std::setw(13) << std::fixed << std::setprecision(2)
                      << (static_cast<double>(rate) / static_cast<double>(base_spd)) << "x";
        } else {
            std::cout << std::setw(14) << "-";
        }
        std::cout << "\n";
    }
}

}  // namespace

int main() {
    const auto root = std::filesystem::path("/tmp/mylogger_vs_spdlog");
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    std::cout << "=== Mylogger 分片 vs spdlog：纯入队 + e2e 对照 ===\n"
              << "机器核数约 " << std::thread::hardware_concurrency() << "\n"
              << "纯入队 = 只计 Log()/info() 循环，计时区内不 Flush；"
                 "生产者侧含 Format/序列化后投递。\n"
              << "Mylogger 消费侧仍有 Proto+Zstd+AES；spdlog 为明文异步写盘。\n";

    struct Case {
        size_t msg_len;
        size_t n;
        int producers;
        const char* title;
    };
    const Case cases[] = {
        {64, 80000, 1, "64B · 单生产者"},
        {64, 120000, 4, "64B · 四生产者"},
        {4096, 30000, 1, "4KB · 单生产者"},
        {4096, 40000, 4, "4KB · 四生产者"},
    };

    for (const auto& c : cases) {
        std::vector<Row> rows;
        for (int shards : {1, 2, 4}) {
            rows.push_back(BenchMylogger(shards, c.msg_len, c.n, c.producers, root));
        }
        for (int workers : {1, 4}) {
            rows.push_back(BenchSpdlog(workers, c.msg_len, c.n, c.producers, root));
        }
        const auto label = std::string(c.title) + "  n=" + std::to_string(c.n);
        PrintTable(label, rows, "burst");
        PrintTable(label, rows, "e2e");
    }

    std::filesystem::remove_all(root);
    std::cout << "\n完成，已清理 " << root << "\n";
    return 0;
}
