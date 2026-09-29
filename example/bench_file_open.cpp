// Reproducible e2e bench for "keep log file open" before/after comparison.
// Uses small cache_size so CacheToFile_ runs often (open/close would dominate more).

#include "effective_sink.h"
#include "log_handle.h"

#include <chrono>
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

struct Row {
    const char* name;
    long e2e;
    long burst;
    uint64_t drop_q;
    uint64_t drop_c;
};

Row RunOnce(const char* name, size_t msg_len, size_t n, int producers, logger::megabytes single_size) {
    const auto dir = std::filesystem::path("/tmp/mylogger_bench_file_open") / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    logger::EffectiveSink::Conf conf;
    conf.dir = dir;
    conf.prefix = "fo";
    conf.pub_key = kPubKey;
    conf.cache_size = logger::megabytes(1);  // flush often
    conf.single_size = single_size;
    conf.total_size = logger::megabytes(256);
    conf.max_pending_tasks = 5000000;
    conf.max_pending_bytes = size_t{1} << 30;
    conf.drop_below = logger::LogLevel::kTrace;

    auto sink = std::make_shared<logger::EffectiveSink>(std::move(conf));
    logger::LogHandle handle({sink});
    logger::SourceLocation loc{};
    std::string msg(msg_len, 'x');

    for (size_t i = 0; i < 1000; ++i) {
        handle.Log(logger::LogLevel::kInfo, loc, msg);
    }
    sink->Flush();

    auto run_n = [&] {
        if (producers <= 1) {
            for (size_t i = 0; i < n; ++i) {
                handle.Log(logger::LogLevel::kInfo, loc, msg);
            }
            return;
        }
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
    };

    auto b0 = std::chrono::steady_clock::now();
    run_n();
    auto b1 = std::chrono::steady_clock::now();
    sink->Flush();

    auto st0 = sink->GetStats();
    auto t0 = std::chrono::steady_clock::now();
    run_n();
    sink->Flush();
    auto t1 = std::chrono::steady_clock::now();
    auto st1 = sink->GetStats();

    Row r;
    r.name = name;
    r.burst = static_cast<long>(n / std::chrono::duration<double>(b1 - b0).count());
    r.e2e = static_cast<long>(n / std::chrono::duration<double>(t1 - t0).count());
    r.drop_q = st1.dropped_by_queue - st0.dropped_by_queue;
    r.drop_c = st1.dropped_by_cache - st0.dropped_by_cache;
    return r;
}

}  // namespace

int main() {
    std::cout << "=== bench_file_open (cache=1MB, force frequent CacheToFile_) ===\n";
    const Row rows[] = {
        RunOnce("64B_p1", 64, 80000, 1, logger::megabytes(8)),
        RunOnce("64B_p4", 64, 120000, 4, logger::megabytes(8)),
        RunOnce("4KB_p1", 4096, 30000, 1, logger::megabytes(8)),
        RunOnce("4KB_p4", 4096, 40000, 4, logger::megabytes(8)),
        // force file rotate during run
        RunOnce("4KB_p4_rotate", 4096, 40000, 4, logger::megabytes(2)),
    };
    for (const auto& r : rows) {
        std::cout << r.name << " e2e=" << r.e2e << "/s burst=" << r.burst << "/s drop_q=" << r.drop_q
                  << " drop_c=" << r.drop_c << "\n";
    }
    std::filesystem::remove_all("/tmp/mylogger_bench_file_open");
    return 0;
}
