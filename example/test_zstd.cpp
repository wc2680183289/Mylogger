#include "zstd_compress.h"
#include <iostream>
#include <string>
#include <vector>

int main() {
    logger::compress::ZstdCompress zstd;

    std::string str = "hello zstd compress, hello zstd compress";
    size_t bound = zstd.CompressedBound(str.size());
    std::vector<char> out(bound);

    size_t n = zstd.Compress(str.data(), str.size(), out.data(), out.size());
    if(n == 0) {
        std::cerr << "Compress failed\n";
        return 1;
    }

    std::cout << "compressed size = " << n << "\n";

    std::string orig = zstd.Decompress(out.data(), out.size());

    std::cout << "decompressed=[" << orig << "]\n";

    if(orig != str) {
        std::cerr << "mismatch\n";
        return 1;
    }
    std::cout << "ok\n";

    return 0;
}