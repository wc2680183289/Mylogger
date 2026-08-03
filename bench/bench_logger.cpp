#include <benchmark/benchmark.h>
#include <memory>
#include <string>

#include "aes_crypt.h"
#include "console_sink.h"
#include "log_handle.h"
#include "sink.h"
#include "effective_sink.h"
#include "space.h"

#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/null_sink.h>

#include <mutex>
#include <filesystem>

class NullSink final : public logger::LogSink {
public:
    void Log(const logger::LogMsg&) override {}
    void SetFormatter(std::unique_ptr<logger::Formatter>) override {}
    void Flush() override {}
};

static void BM_MyLogger_Log(benchmark::State& state) {
    auto sink = std::make_shared<NullSink>();
    logger::LogHandle handle({sink});
    std::string msg(state.range(0), 'x');
    logger::SourceLocation loc{};  // 或带 __FILE__ 的，两边习惯保持简单即可
  
    for (auto _ : state) {
      handle.Log(logger::LogLevel::kInfo, loc, msg);
    }
    state.SetItemsProcessed(state.iterations());
}

static void BM_Spdlog_Log(benchmark::State& state) {
    auto sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    auto lg = std::make_shared<spdlog::logger>("bench", sink);
    lg->set_pattern("%v");  // 尽量少格式化开销，接近「只输出正文」
    std::string msg(state.range(0), 'x');
  
    for (auto _ : state) {
      lg->info("{}", msg);
    }
    state.SetItemsProcessed(state.iterations());
}

static void BM_AesRoundTrip(benchmark::State& state) {
  auto key = logger::crypt::AESCrypt::GenerateKey();
  logger::crypt::AESCrypt aes(key);
  std::string plain(state.range(0), 'x');
  std::string cipher;

  for (auto _ : state) {
    cipher.clear();
    aes.Encrypt(plain.data(), plain.size(), cipher);
    auto out = aes.Decrypt(cipher.data(), cipher.size());
    benchmark::DoNotOptimize(out);
  }
  state.SetBytesProcessed(state.iterations() * state.range(0));
}

static void BM_EffectiveSink_Log(benchmark::State& state) {
    static std::once_flag once;
    static std::shared_ptr<logger::LogHandle> handle;
  
    std::call_once(once, [] {
      std::filesystem::create_directories("/tmp/bench_eff");
      logger::EffectiveSink::Conf conf;
      conf.dir = "/tmp/bench_eff";
      conf.prefix = "bench";
      conf.pub_key =
          "04827405069030E26A211C973C8710E6FBE79B5CAA364AC111FB171311902277537F8852EADD17EB339EB7CD0BA2490A58CDED2C702DFC1E"
          "FC7EDB544B869F039C";
      conf.single_size = logger::megabytes(64);
      conf.total_size = logger::megabytes(256);
      auto sink = std::make_shared<logger::EffectiveSink>(conf);
      handle = std::make_shared<logger::LogHandle>(logger::LogSinkPtrList{sink});
    });
  
    std::string msg(static_cast<size_t>(state.range(0)), 'x');
    logger::SourceLocation loc{};
    for (auto _ : state) {
      handle->Log(logger::LogLevel::kInfo, loc, msg);
    }
    state.SetItemsProcessed(state.iterations());
  }
  
  static void BM_SpdlogAsync_Log(benchmark::State& state) {
    static std::once_flag once;
    static std::shared_ptr<spdlog::logger> logger;
  
    std::call_once(once, [] {
      std::filesystem::create_directories("/tmp/bench_spdlog");
      spdlog::init_thread_pool(8192, 1);
      logger = spdlog::create_async<spdlog::sinks::basic_file_sink_mt>(
          "spd_bench", "/tmp/bench_spdlog/spd.log");
    });
  
    std::string msg(static_cast<size_t>(state.range(0)), 'x');
    for (auto _ : state) {
      logger->info("{}", msg);
    }
    state.SetItemsProcessed(state.iterations());
  }
  
  BENCHMARK(BM_EffectiveSink_Log)->Arg(64)->Threads(1);
  BENCHMARK(BM_EffectiveSink_Log)->Arg(64)->Threads(4);
  BENCHMARK(BM_EffectiveSink_Log)->Arg(4096)->Threads(1);
  BENCHMARK(BM_EffectiveSink_Log)->Arg(4096)->Threads(4);
  
  BENCHMARK(BM_SpdlogAsync_Log)->Arg(64)->Threads(1);
  BENCHMARK(BM_SpdlogAsync_Log)->Arg(64)->Threads(4);
  BENCHMARK(BM_SpdlogAsync_Log)->Arg(4096)->Threads(1);
  BENCHMARK(BM_SpdlogAsync_Log)->Arg(4096)->Threads(4);

BENCHMARK(BM_AesRoundTrip)->Arg(64)->Arg(1024)->Arg(4096)->Threads(4);
BENCHMARK(BM_MyLogger_Log)->Arg(64)->Arg(1024)->Threads(4);
BENCHMARK(BM_Spdlog_Log)->Arg(64)->Arg(1024)->Threads(4);