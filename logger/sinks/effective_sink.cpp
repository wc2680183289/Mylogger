#include "effective_sink.h"

#include <algorithm>
#include <fstream>
#include <utility>

#include "zlib_compress.h"
#include "zstd_compress.h"
#include "aes_crypt.h"
#include "defer.h"
#include "effective_formatter.h"
#include "internal_log.h"
#include "file_util.h"
#include "sys_util.h"
#include "timer_count.h"

namespace logger {

EffectiveSink::EffectiveSink(Conf conf) : conf_(std::move(conf)) {
    LOG_INFO("EffectiveSink : dir = {}, prefix = {}, pub_key = {}, interval = {}, single_size = {}, total_size = {}, "
             "cache_size = {}, max_pending_tasks = {}, max_pending_bytes = {}",
             conf_.dir.string(), conf_.prefix, conf_.pub_key, conf_.interval.count(),
             conf_.single_size.count(), conf_.total_size.count(), conf_.cache_size.count(),
             conf_.max_pending_tasks, conf_.max_pending_bytes);
    if(!std::filesystem::exists(conf_.dir)) {
        std::filesystem::create_directories(conf_.dir);
    }
    task_runner_ = NEW_TASK_RUNNER(20000);
    formatter_ = std::make_unique<EffectiveFormatter>();
    auto ecdh_key = crypt::GenECDHKey();
    auto client_key = std::get<0>(ecdh_key);
    client_pub_key_ = std::get<1>(ecdh_key);
    LOG_INFO("EffectiveSink client_pub_key size = {}", client_pub_key_.size());
    auto ser_pub_key = crypt::HexKeyToBinary(conf_.pub_key);
    auto shared_secret = crypt::GenECDHSharedSecret(client_key, ser_pub_key);
    std::cout << "EffectiveSink::EffectiveSink : shared_secret = " << logger::crypt::BinaryKeyToHex(shared_secret) << "\n";
    crypt_ = std::make_unique<crypt::AESCrypt>(shared_secret);
    compress_ = std::make_unique<compress::ZstdCompress>();

    const size_t cache_cap = space_cast<bytes>(conf_.cache_size).count();
    master_cache_ = std::make_unique<MMapAux>(conf_.dir / "master.cache", cache_cap);
    slave_cache_ = std::make_unique<MMapAux>(conf_.dir / "slave.cache", cache_cap);
    if(!master_cache_ || !slave_cache_) {
        throw std::runtime_error("master_cache or slave_cache create failed");
    }

    if(!slave_cache_->Empty()) {
        is_slave_free_.store(false);
        PrepareToFile_();
        WAIT_TASK_IDLE(task_runner_);
    }

    if(!master_cache_->Empty()){
        if(is_slave_free_.load()) {
            is_slave_free_.store(false);
            SwapCache_();
        }
        PrepareToFile_();
    }

    POST_REPEATED_TASK(task_runner_, [this](){ EliminateFiles_(); }, conf_.interval, -1);
}

void EffectiveSink::SwapCache_() {
    std::lock_guard<std::mutex>lk(mtx_);
    std::swap(master_cache_, slave_cache_);
}

void EffectiveSink::Flush() {
    POST_TASK(task_runner_, [this]() {
        if(is_slave_free_.load()) {
            is_slave_free_.store(false);
            SwapCache_();
        }
        CacheToFile_();
    });
    WAIT_TASK_IDLE(task_runner_);
}

bool EffectiveSink::TryWriteToCache_(const void* data, uint32_t size) {
    std::lock_guard<std::mutex> lk(mtx_);
    const size_t before = master_cache_->Size();
    detail::ItemHeader item_header;
    item_header.size = size;
    if (!master_cache_->Push(&item_header, sizeof(item_header))) {
        return false;
    }
    if (!master_cache_->Push(data, size)) {
        // A3: roll back header-only item
        if (!master_cache_->Resize(before)) {
            LOG_ERROR("EffectiveSink::TryWriteToCache_: rollback Resize failed, before={}", before);
        }
        return false;
    }
    return true;
}

bool EffectiveSink::MakeRoom_() {
    // Same TaskRunner thread: finish flush / swap to free master (A1+A4).
    if (!is_slave_free_.load()) {
        CacheToFile_();
    }
    if (is_slave_free_.load()) {
        is_slave_free_.store(false);
        SwapCache_();
        CacheToFile_();
        return true;
    }
    return false;
}

bool EffectiveSink::WriteToCacheWithRetry_(const void* data, uint32_t size) {
    const int retries = std::max(1, conf_.cache_write_retries);
    for (int i = 0; i < retries; ++i) {
        if (TryWriteToCache_(data, size)) {
            return true;
        }
        cache_write_retries_.fetch_add(1, std::memory_order_relaxed);
        if (!MakeRoom_()) {
            LOG_ERROR("EffectiveSink::WriteToCacheWithRetry_: MakeRoom_ failed, attempt={}/{}", i + 1, retries);
        }
    }
    return TryWriteToCache_(data, size);
}

void EffectiveSink::ProcessLog_(std::string formatted) {
    if(master_cache_->Empty()) {
        compress_->ResetStream();
    }

    size_t encrypted_size = 0;
    {
        std::lock_guard<std::mutex>lk(mtx_);
        compressed_buf_.resize(compress_->CompressedBound(formatted.size()));
        size_t compressed_size = compress_->Compress(formatted.data(), formatted.size(), compressed_buf_.data(), compressed_buf_.size());
        if(compressed_size == 0) {
            LOG_ERROR("EffectiveSink::ProcessLog_: compress failed");
            return;
        }
        encryped_buf_.clear();
        encryped_buf_.resize(compressed_size + 16);
        crypt_->Encrypt(compressed_buf_.data(), compressed_size, encryped_buf_);
        if(encryped_buf_.empty()) {
            LOG_ERROR("EffectiveSink::ProcessLog_: encrypt failed");
            return;
        }
        encrypted_size = encryped_buf_.size();
    }

    // Write/retry outside compress lock section so SwapCache_ can take mtx_.
    if (!WriteToCacheWithRetry_(encryped_buf_.data(), static_cast<uint32_t>(encrypted_size))) {
        dropped_by_cache_.fetch_add(1, std::memory_order_relaxed);
        LOG_ERROR("EffectiveSink::ProcessLog_: cache write failed after retries, dropped_by_cache={}",
                  dropped_by_cache_.load(std::memory_order_relaxed));
        return;
    }

    if(NeedCacheToFile_()){
        if(is_slave_free_.load()){
            is_slave_free_.store(false);
            SwapCache_();
        } 
        CacheToFile_();
    }
}

bool EffectiveSink::QueueFull_() const {
    return pending_tasks_.load(std::memory_order_relaxed) >= conf_.max_pending_tasks ||
           pending_bytes_.load(std::memory_order_relaxed) >= conf_.max_pending_bytes;
}

void EffectiveSink::Log(const LogMsg& msg) {
    // P-drop-low before Format when full
    if (QueueFull_() && msg.level < conf_.drop_below) {
        dropped_by_queue_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    static thread_local MemoryBuf buf;
    formatter_->Format(msg, &buf);

    std::string payload(buf.data(), buf.size());
    const std::size_t nbytes = payload.size();
    pending_tasks_.fetch_add(1, std::memory_order_relaxed);
    pending_bytes_.fetch_add(nbytes, std::memory_order_relaxed);

    auto task = [this, payload = std::move(payload), nbytes]() mutable {
        struct PendingGuard {
            EffectiveSink* self;
            std::size_t n;
            ~PendingGuard() {
                self->pending_tasks_.fetch_sub(1, std::memory_order_relaxed);
                self->pending_bytes_.fetch_sub(n, std::memory_order_relaxed);
            }
        } guard{this, nbytes};
        ProcessLog_(std::move(payload));
    };
    POST_TASK(task_runner_, task);
}

void EffectiveSink::SetFormatter(std::unique_ptr<Formatter> formatter) {}

EffectiveSink::Stats EffectiveSink::GetStats() const {
    Stats s;
    s.dropped_by_queue = dropped_by_queue_.load(std::memory_order_relaxed);
    s.dropped_by_cache = dropped_by_cache_.load(std::memory_order_relaxed);
    s.cache_write_retries = cache_write_retries_.load(std::memory_order_relaxed);
    s.pending_tasks = pending_tasks_.load(std::memory_order_relaxed);
    s.pending_bytes = pending_bytes_.load(std::memory_order_relaxed);
    return s;
}

bool EffectiveSink::NeedCacheToFile_() {
    return master_cache_->GetRatio() > 0.8;
}

void EffectiveSink::PrepareToFile_() {
    POST_TASK(task_runner_, [this]() { CacheToFile_(); });
}

void EffectiveSink::CacheToFile_() {
    if(is_slave_free_.load()){
        return;
    }

    if(slave_cache_->Empty()) {
        is_slave_free_.store(true);
        return;
    }

    {
        auto file_path = GetFilePath_();
        detail::ChunkHeader chunk_header;
        chunk_header.size = slave_cache_->Size();
        memcpy(chunk_header.pub_key, client_pub_key_.data(), client_pub_key_.size());
        std::ofstream ofs(file_path, std::ios::binary | std::ios::app);
        ofs.write(reinterpret_cast<char*>(&chunk_header), sizeof(chunk_header));
        ofs.write(reinterpret_cast<char*>(slave_cache_->Data()), slave_cache_->Size());
        ofs.close();
    }
    slave_cache_->Clear();
    is_slave_free_.store(true);
}

std::filesystem::path EffectiveSink::GetFilePath_() {
    auto GetDateTimePath = [this]() -> std::filesystem::path {
        std::time_t now = std::time(nullptr);
        std::tm tm;
        LocalTime(&tm, &now);
        char time_buf[32] = {0};
        std::strftime(time_buf, sizeof(time_buf), "%Y%m%d%H%M%S", &tm);
        return (conf_.dir / (conf_.prefix + "_" + time_buf));
    };

    if (log_file_path_.empty()) {
        log_file_path_ = GetDateTimePath().string() + ".log";
    } else {
        auto file_size = fs::GetFileSize(log_file_path_);
        bytes single_bytes = space_cast<bytes>(conf_.single_size);
        if(file_size > single_bytes.count()) {
            auto date_time_path = GetDateTimePath();
            auto file_path = date_time_path.string() + ".log";
            if(std::filesystem::exists(file_path)) {
                int idx = 0;
                for(auto& p : std::filesystem::directory_iterator(conf_.dir)) {
                    if(p.path().filename().string().find(date_time_path.string()) != std::string::npos) {
                        idx++;
                    }
                }
                log_file_path_ = date_time_path.string() + "_" + std::to_string(idx) + ".log";
            }
            else {
                log_file_path_ = file_path;
            }
        }
    }
    LOG_INFO("EffectiveSink::GetFilePath_ : log_file_path = {}", log_file_path_.string());
    return log_file_path_;
}

void EffectiveSink::EliminateFiles_() {
    LOG_INFO("EffectiveSink::EliminateFiles_: start");
    std::vector<std::filesystem::path> files;
    for(auto& p : std::filesystem::directory_iterator(conf_.dir)) {
        if(p.path().extension() == ".log") {
            files.push_back(p.path());
        }
    }
    std::sort(files.begin(), files.end(), [](const std::filesystem::path& lhs, const std::filesystem::path& rhs) {
        return std::filesystem::last_write_time(lhs) > std::filesystem::last_write_time(rhs);
    });

    size_t total_bytes = space_cast<bytes>(conf_.total_size).count();
    size_t used_bytes = 0;
    for (auto& file : files) {
        used_bytes += fs::GetFileSize(file);
        if(used_bytes > total_bytes) {
            LOG_INFO("EffectiveSink::EliminateFiles_: remove file = {}", file.string());
            std::filesystem::remove(file);
        }
    }
}

}
