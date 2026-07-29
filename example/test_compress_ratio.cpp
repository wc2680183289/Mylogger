#include "zlib_compress.h"
#include "zstd_compress.h"
#include <iostream>
#include <string>
#include <vector>

void PrintRatio(const char* name, size_t origin, size_t compressed) {
    double ratio = compressed * 1.0 / origin;
    std::cout << name << " origin = " << origin << " compressed = " << compressed << " ratio = " << ratio <<"\n";
}

int main() {

    std::string str;
    for(int i = 0; i < 100; i++) {
        str += "hello compress compare, hello compress compare";
    }

    logger::compress::ZlibCompress zlib;
    zlib.ResetStream();
    std::vector<char>zlib_out(zlib.CompressedBound(str.size()));
    size_t z_n = zlib.Compress(str.data(), str.size(), zlib_out.data(), zlib_out.size());

    if(z_n == 0) {
        std::cerr << "zlib compress failed\n";
        return 1;
    }

    logger::compress::ZstdCompress zstd;
    std::vector<char>zstd_out(zstd.CompressedBound(str.size()));
    size_t zs_n = zstd.Compress(str.data(), str.size(), zstd_out.data(), zstd_out.size());

    if(zs_n == 0) {
        std::cerr << "zstd compress failed\n";
        return 1;
    }
    PrintRatio("zlib", str.size(), z_n);
    PrintRatio("zstd", str.size(), zs_n);
    std::cout << "ok\n";
    return 0;
}