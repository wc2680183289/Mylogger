#include <gtest/gtest.h>
#include "aes_crypt.h"

TEST(CryptAES, RoundTrip) {
  auto key = logger::crypt::AESCrypt::GenerateKey();
  logger::crypt::AESCrypt aes(key);

  std::string plain = "Hello, AES!";
  std::string cipher;
  aes.Encrypt(plain.data(), plain.size(), cipher);

  ASSERT_FALSE(cipher.empty());
  EXPECT_NE(cipher, plain);
  EXPECT_EQ(aes.Decrypt(cipher.data(), cipher.size()), plain);
}