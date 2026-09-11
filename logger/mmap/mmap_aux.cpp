#include "mmap_aux.h"
#include "file_util.h"
#include "sys_util.h"
#include "internal_log.h"

#include <sys/mman.h>

#include <cstring>
#include <algorithm>

static constexpr size_t kDefaultCapacity = 512 * 1024;

namespace logger{
    MMapAux::MMapAux(fpath file_path, size_t max_capacity)
        : capacity_(0), max_capacity_(max_capacity), file_path_(std::move(file_path)), handle_(nullptr) {
        if (max_capacity_ < sizeof(MmapHeader) + 1) {
            max_capacity_ = kDefaultMaxCapacity;
        }
        size_t file_size = fs::GetFileSize(file_path_);
        size_t init_size = std::max(file_size, kDefaultCapacity);
        // Cap fresh growth; still map an existing oversized file for recovery.
        if (file_size <= max_capacity_) {
            init_size = std::min(init_size, max_capacity_);
        } else {
            init_size = file_size;
            LOG_ERROR("MMapAux: existing file larger than max_capacity, mapping file_size={}, max={}",
                      file_size, max_capacity_);
        }
        if (!Reserve_(init_size)) {
            LOG_ERROR("MMapAux: initial Reserve_ failed, path={}, init_size={}", file_path_.string(), init_size);
            handle_ = nullptr;
            capacity_ = 0;
            return;
        }
        Init_();
    }

    MMapAux::~MMapAux() {
        UnMap_();
    }
    
    static size_t GetValidCapacity_(size_t size){
        size_t page_size = GetPageSize();
        return (size + page_size - 1)/page_size * page_size;
    }
    
    bool MMapAux::Reserve_(size_t new_size){
        if(new_size <= capacity_){
            return true;
        }
        new_size = GetValidCapacity_(new_size);
        if (new_size > max_capacity_) {
            LOG_ERROR("MMapAux::Reserve_: exceeds max_capacity, path={}, need={}, max={}",
                      file_path_.string(), new_size, max_capacity_);
            return false;
        }

        void* new_handle = nullptr;
        if (!MapFile_(new_size, &new_handle)) {
            return false;
        }

        if (IsMapped_()) {
            munmap(handle_, capacity_);
        }
        handle_ = new_handle;
        capacity_ = new_size;
        return true;
    }
    
    MMapAux::MmapHeader* MMapAux::Header_() const {
        if(!IsMapped_()){
            return nullptr;
        }
        if(capacity_ < sizeof(MmapHeader)){
            return nullptr;
        }
        return static_cast<MmapHeader*>(handle_);
    }
    
    void MMapAux::Init_(){
        MmapHeader* header = Header_();
        if(!header){
            return;
        }
        if(header->magic != MmapHeader::kMagic){
            header->magic = MmapHeader::kMagic;
            header->size = 0;
        }
    }
    
    bool MMapAux::EnsureCapacity_(size_t new_size){
        size_t real_size = new_size + sizeof(MmapHeader);
        if(real_size <= capacity_){
            return true;
        }
        if (real_size > max_capacity_) {
            return false;
        }
        auto new_capacity = capacity_;
        while(new_capacity < real_size){
            new_capacity += GetPageSize();
        }
        if (new_capacity > max_capacity_) {
            new_capacity = max_capacity_;
        }
        if (new_capacity < real_size) {
            return false;
        }
        return Reserve_(new_capacity);
    }
    
    bool MMapAux::IsValid_() const {
        MmapHeader* header = Header_();
        if(!header){
            return false;
        }
    
        return header->magic == MmapHeader::kMagic;
    }
    
    bool MMapAux::Resize(size_t new_size){
        if(!IsValid_()){
            return false;
        }
        size_t real_size = new_size + sizeof(MmapHeader);
        if (real_size > capacity_) {
            if (!EnsureCapacity_(new_size)) {
                LOG_ERROR("MMapAux::Resize: EnsureCapacity_ failed, path={}, new_size={}", file_path_.string(), new_size);
                return false;
            }
        }
        Header_()->size = static_cast<uint32_t>(new_size);
        return true;
    }
    
    uint8_t* MMapAux::Data() const {
        if(!IsValid_()){
            return nullptr;
        }
        return static_cast<uint8_t*>(handle_) + sizeof(MmapHeader);
    }
    
    size_t MMapAux::Size() const {
        if(!IsValid_()){
            return 0;
        }
        return Header_()->size;
    }
    
    void MMapAux::Clear() {
        if(!IsValid_()){
            return;
        }
        Header_()->size = 0;
    }
    
    bool MMapAux::Push(const void* data, size_t size){
        if(!IsValid_() || !data){
            return false;
        }
    
        size_t new_size = Size() + size;
        if (!EnsureCapacity_(new_size)) {
            LOG_ERROR("MMapAux::Push: EnsureCapacity_ failed, path={}, cur_size={}, need={}, max={}",
                      file_path_.string(), Size(), new_size, max_capacity_);
            return false;
        }
        uint8_t* dst = Data();
        if (!dst) {
            LOG_ERROR("MMapAux::Push: Data() is null after EnsureCapacity_, path={}", file_path_.string());
            return false;
        }
        memcpy(dst + Size(), data, size);
        Header_()->size = static_cast<uint32_t>(new_size);
        return true;
    }
    
    double MMapAux::GetRatio() const{
        if(!IsValid_()){
            return 0;
        }
        size_t denom = Capacity_() - sizeof(MmapHeader);
        if (denom == 0) {
            return 0;
        }
        return static_cast<double>(Size()) / static_cast<double>(denom);
    }
}
