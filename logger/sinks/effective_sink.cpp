#include "effective_sink.h"

#include <fstream>

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
    LOG_INFO("EffectiveSink : dir = {}, prefix = {}, pub_key = {}, interval = {}, single_size = {}, totai_size = {}", conf_.dir, conf_.prefix, conf_.pub_key, 
              conf_.interval, conf_.single_size, conf_.total_size);
    if(!std::filesystem::exists(conf_.dir)) {
        std::filesystem::create_directories(conf_.dir);
    }
    task_runner_ = NEW_TASK_RUNNER(20000);
    formatter_ = std::make_unique<EffectiveFormatter>();
    auto ecdh_key = crypt::GenECDHKey();//生成密钥对
    auto client_key = std::get<0>(ecdh_key);
    client_pub_key_ = std::get<1>(ecdh_key);
    LOG_INFO("EffectiveSink client_pub_key size = {}",client_pub_key.size());
    auto ser_pub_key = crypt::HexKeyToBinary(conf_.pub_key);
    auto shared_secret = crypt::GenECDHSharedSecret(client_key, ser_pub_key);//用己方私钥和对方公钥算出AES所需的共享密钥
    crypt_ = std::make_unique<crypt::AESCrypt>(shared_secret);
    compress_ = std::make_unique<compress::ZstdCompress>();
    master_cache_ = std::make_unique<MMapAux>(conf_.dir / "master.cache");
    slave_cache_ = std::make_unique<MMapAux>(conf_.dir / "slave.cache");
    if(!master_cache_ || !slave_cache_) {
        throw std::runtime_error("master_cache or slave_cache create failed");
    }

    //如果从cache有内容则直接写入文件中，这里通过相同的task_runner_找到同一个任务队列，
    //当WAIT_TASK_IDLE退出阻塞后PrepareToFile已经完成
    if(!slave_cache_->Empty()) {
        is_slave_free_.store(false);
        PrepareToFile_();
        WAIT_TASK_IDLE(task_runner_);
    }

    //如果主cache不为空首先判断从cache是否空闲
    //如果空闲则直接交换主从cache
    //否则直接将从cache内容写入文件
    if(!master_cache_->Empty()){
        if(is_slave_free_.load()) {
            is_slave_free_.store(false);
            SwapCache_();
        }
        PrepareToFile_();
    }

    //不直接清空是因为如果有数据多半是因为上次进程的异常退出
    //此时cache中的数据有可能还没来得及写入日志，所以要先落盘，在CacheToFile_中清空

    //循环次数设置为-1，由于类型是uint64_t, 等效无穷大，一直执行
    POST_REPEATED_TASK(task_runner_, [this](){ EliminateFiles_(); }, conf_.interval, -1);
}

void EffectiveSink::SwapCache_() {
    std::lock_guard<std::mutex>lk(mtx_);
    std::swap(master_cache_, slave_cache_);
}

void EffectiveSink::Flush() {
    PrepareToFile_();
    WAIT_TASK_IDLE(task_runner_);

    if(is_slave_free_.load()){
        is_slave_free_.store(false);
        SwapCache_();
    } 
    PrepareToFile_();
    WAIT_TASK_IDLE(task_runner_);
}

void EffectiveSink::Log(const LogMsg& msg) {
    static thread_local MemoryBuf buf;
    formatter_->Format(msg, &buf);

    if(master_cache_->Empty()){
        compress_->ResetStream();
    }

    {   //先压缩后加密
        std::lock_guard<std::mutex>lk(mtx_);
        compressed_buf_.resize(compress_->CompressedBound(buf.size()));
        size_t compressed_size = compress_->Compress(buf.data(), buf.size(), compressed_buf_.data(), compressed_buf_.size());
        if(compressed_size == 0) {
            LOG_ERROR("EffectiveSink::Log : compress failed");
            return;
        }
        encryped_buf_.clear();
        encryped_buf_.resize(compressed_size + 16);
        crypt_->Encrypt(compressed_buf_.data(), compressed_buf_.size(), encryped_buf_);
        if(encryped_buf_.empty()) {
            LOG_ERROR("EffectiveSink::Log : encrypt failed");
            return;
        }
        WriteToCache_(encryped_buf_.data(), encryped_buf_.size());
    }
    if(NeedCacheToFile_()){
        if(is_slave_free_.load()){
            is_slave_free_.store(false);
            SwapCache_();
        }
        PrepareToFile_();
    }
}

void EffectiveSink::SetFormatter(std::unique_ptr<Formatter> formatter) {}

bool EffectiveSink::NeedCacheToFile_() {
    return master_cache_->GetRatio() > 0.8;
}

void EffectiveSink::WriteToCache_(const void* data, uint32_t size) {
    detail::ItemHeader item_header;
    item_header.size = size;
    //每条日志由头和体构成，头存放了消息体的大小
    master_cache_->Push(&item_header, sizeof(item_header));
    master_cache_->Push(data, size);
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

    {//所有日志打包成块一起落盘，格式与单条日志类似
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
    //给日志文件命名
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
        //目录下有多个文件
        if(file_size > single_bytes.count()) {
            auto date_time_path = GetDateTimePath();
            auto file_path = date_time_path.string() + ".log";
            //已存在就根据下标向后移
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
    //超出大小时淘汰老日志
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