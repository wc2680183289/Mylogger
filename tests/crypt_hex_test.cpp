#include <gtest/gtest.h>
#include "crypt.h"

TEST(CryptHex, RoundTrip) {
  std::string bin = "\x01\xAB\xFF";
  std::string hex = logger::crypt::BinaryKeyToHex(bin);
  EXPECT_EQ(hex, "01ABFF");
  EXPECT_EQ(logger::crypt::HexKeyToBinary(hex), bin);
}