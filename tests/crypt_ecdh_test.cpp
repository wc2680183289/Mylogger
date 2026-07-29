#include <gtest/gtest.h>
#include "crypt.h"

TEST(CryptECDH, SharedSecretMatch) {
  auto [pri_a, pub_a] = logger::crypt::GenECDHKey();
  auto [pri_b, pub_b] = logger::crypt::GenECDHKey();

  EXPECT_EQ(pri_a.size(), 32u);
  EXPECT_FALSE(pub_a.empty());

  auto s1 = logger::crypt::GenECDHSharedSecret(pri_a, pub_b);
  auto s2 = logger::crypt::GenECDHSharedSecret(pri_b, pub_a);

  ASSERT_FALSE(s1.empty());
  EXPECT_EQ(s1, s2);
}