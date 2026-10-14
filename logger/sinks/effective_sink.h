#pragma once 

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>

#include "compress.h"
#include "context.h"
#include "crypt.h"
#include "log_common.h"
#include "mmap_aux.h"
#include "sink.h"
#include "space.h"

namespace logger {

namespace detail {
struct ChunkHeader {
    static constexpr uint64_t kMagic = 0xdeadbeefdada1100;
    uint64_t magic;
    uint64_t size;
    char pub_key[128];

    ChunkHeader() : magic(kMagic), size(0) {}
};

struct ItemHeader {
    static constexpr uint32_t kMagic = 0xbe5fba11;
    uint32_t magic;
    uint32_t size;

    ItemHeader() : magic(kMagic), size(0) {}
};

}

class EffectiveSink final : public LogSink {
public:
    struct Conf {
        std::filesystem::path dir;
        std::string prefix;
        std::string pub_key;
        std::chrono::minutes interval{10};
        megabytes single_size{4};
        megabytes total_size{100};
        // mmap cache hard cap (A2)
        megabytes cache_size{4};
        // Q3 pending limits for EffectiveSink::Log only
        std::size_t max_pending_tasks{10000};
        std::size_t max_pending_bytes{32 * 1024 * 1024};
        // P-drop-low: levels < drop_below are dropped when queue is full
        LogLevel drop_below{LogLevel::kError};
        // A1+A4: flush/swap retries before giving up a cache write
        int cache_write_retries{3};
    };

    struct Stats {
        uint64_t dropped_by_queue{0};
        uint64_t dropped_by_cache{0};
        uint64_t cache_write_retries{0};
        uint64_t pending_tasks{0};
        uint64_t pending_bytes{0};
    };

    explicit EffectiveSink(Conf conf);

    ~EffectiveSink() override = default;

    void Log(const LogMsg& msg) override;

    void SetFormatter(std::unique_ptr<Formatter> formatter) override;

    void Flush() override;

    Stats GetStats() const;

private:
    void SwapCache_();

    bool NeedCacheToFile_();

    bool TryWriteToCache_(const void* data, uint32_t size);

    bool MakeRoom_();

    bool WriteToCacheWithRetry_(const void* data, uint32_t size);

    void PrepareToFile_();

    void CacheToFile_();

    std::filesystem::path GetFilePath_();

    void EliminateFiles_();

    void ProcessLog_(std::string formatted);

    bool QueueFull_() const;

private:
    Conf conf_;
    std::mutex mtx_;
    std::unique_ptr<Formatter> formatter_;
    ctx::TaskRunnerTag task_runner_;
    std::unique_ptr<crypt::Crypt> crypt_;
    std::unique_ptr<compress::Compression> compress_;
    std::unique_ptr<MMapAux> master_cache_;
    std::unique_ptr<MMapAux> slave_cache_;
    std::filesystem::path log_file_path_;
    std::string client_pub_key_;
    std::string compressed_buf_;
    std::string encryped_buf_;
    std::atomic<bool> is_slave_free_{true};

    std::atomic<std::size_t> pending_tasks_{0};
    std::atomic<std::size_t> pending_bytes_{0};
    std::atomic<uint64_t> dropped_by_queue_{0};
    std::atomic<uint64_t> dropped_by_cache_{0};
    std::atomic<uint64_t> cache_write_retries_{0};
};

}
