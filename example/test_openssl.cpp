#include <openssl/evp.h>
#include <openssl/rand.h>
#include <iostream>

int main() {
  unsigned char buf[16];
  if (RAND_bytes(buf, sizeof(buf)) != 1) {
    std::cerr << "RAND_bytes failed\n";
    return 1;
  }
  std::cout << "openssl ok, OpenSSL_version=" << OpenSSL_version(OPENSSL_VERSION) << "\n";
  return 0;
}