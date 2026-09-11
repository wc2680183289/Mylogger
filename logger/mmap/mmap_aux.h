#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>

namespace logger{
class MMapAux {
public:
    using fpath = std::filesystem::path;

    static constexpr size_t kDefaultMaxCapacity = 4 * 1024 * 1024;

    explicit MMapAux(fpath file_path, size_t max_capacity = kDefaultMaxCapacity);

    ~MMapAux();

    MMapAux(const MMapAux&) = delete;

    MMapAux& operator=(const MMapAux&) = delete;

    // Grow or shrink logical payload size. Returns false if grow fails.
    bool Resize(size_t new_size);

    uint8_t* Data() const;

    size_t Size() const;

    void Clear();

    // Append bytes; returns false if cannot grow (incl. max_capacity) or invalid.
    bool Push(const void* data, size_t size);

    double GetRatio() const;

    size_t Capacity() const noexcept { return capacity_; }

    size_t MaxCapacity() const noexcept { return max_capacity_; }

    inline bool Empty() const { return Size() == 0; }
private:
    struct MmapHeader {
        static constexpr uint32_t kMagic = 0xdeadbeef;
        uint32_t magic = kMagic;
        uint32_t size;
    };
    
private:
    bool Reserve_(size_t new_size);

    bool EnsureCapacity_(size_t new_size);

    size_t Capacity_() const noexcept { return capacity_ ;}

    bool MapFile_(size_t capacity, void** out_handle);

    bool TryMap_(size_t capacity);

    void UnMap_();

    void Sync_();

    bool IsMapped_() const;

    bool IsValid_() const;

    MmapHeader* Header_() const;

    void Init_();

private:
    size_t capacity_;
    size_t max_capacity_;
    fpath file_path_;
    void* handle_;
};

}
