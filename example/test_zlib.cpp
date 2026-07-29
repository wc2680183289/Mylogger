#include <zlib_compress.h>
#include <iostream>
#include <string>
#include <vector>

int main() {
  logger::compress::ZlibCompress zlib;
  zlib.ResetStream();

  std::string src = "hello zlib compress, hello zlib compress";
  size_t bound = zlib.CompressedBound(src.size());
  std::vector<char> out(bound);

  size_t n = zlib.Compress(src.data(), src.size(), out.data(), out.size());
  if(n == 0) {
    std::cerr << "Compress failed\n";
    return 1;
  }
  std::cout << "compressed size = " << n << "\n";

  std::string back = zlib.Decompress(out.data(), n);
  std::cout << "decompressed=[" << back << "]\n";

  if (back != src) {
    std::cerr << "mismatch\n";
    return 1;
  }
  std::cout << "ok\n";
  return 0;
}