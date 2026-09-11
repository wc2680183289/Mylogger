#include "mmap_aux.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "defer.h"
#include "internal_log.h"

namespace logger{

bool MMapAux::IsMapped_() const {
    return handle_ != nullptr && handle_ != MAP_FAILED;
}

bool MMapAux::MapFile_(size_t capacity, void** out_handle) {
    if (!out_handle) {
        return false;
    }
    *out_handle = nullptr;

    int fd = open(file_path_.string().c_str(), O_RDWR | O_CREAT, S_IRWXU);
    LOG_DEFER {
        if (fd != -1) {
            close(fd);
        }
    };

    if (fd == -1) {
        LOG_ERROR("MMapAux::MapFile_: open failed, path={}", file_path_.string());
        return false;
    }
    if (ftruncate(fd, static_cast<off_t>(capacity)) != 0) {
        LOG_ERROR("MMapAux::MapFile_: ftruncate failed, path={}, capacity={}", file_path_.string(), capacity);
        return false;
    }

    void* mapped = mmap(NULL, capacity, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) {
        LOG_ERROR("MMapAux::MapFile_: mmap failed, path={}, capacity={}", file_path_.string(), capacity);
        return false;
    }
    *out_handle = mapped;
    return true;
}

bool MMapAux::TryMap_(size_t capacity) {
    void* mapped = nullptr;
    if (!MapFile_(capacity, &mapped)) {
        handle_ = nullptr;
        return false;
    }
    handle_ = mapped;
    return true;
}

void MMapAux::UnMap_() {
    if (IsMapped_()) {
        munmap(handle_, capacity_);
    }
    handle_ = nullptr;
}

void MMapAux::Sync_() {
    if (IsMapped_()) {
        msync(handle_, capacity_, MS_SYNC);
    }
}

}
