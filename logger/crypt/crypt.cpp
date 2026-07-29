#include "crypt.h"

#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <string>
#include <cctype>
#include <stdexcept>

namespace logger {
namespace crypt {

std::string BinaryKeyToHex(const std::string& binary_key) {
    static const char* str = "0123456789ABCDEF";
    std::string res;
    res.resize(binary_key.size() * 2);
    for(int i = 0; i < binary_key.size(); i++) {
        unsigned char s = static_cast<unsigned char>(binary_key[i]);
        res[2 * i] = str[s >> 4];
        res[2 * i + 1] = str[s & 0x0F];
    }
    return res;
}

std::string HexKeyToBinary(const std::string& hex_key) {
    if(hex_key.size() % 2) {
        throw std::invalid_argument("hex length must be even");
    }

    auto transf = [](char c) {
        if(c >= '0' && c <= '9'){
            return c - '0';
        }
        if(c >= 'a' && c <= 'f'){
            return c - 'a' + 10;
        }
        if(c >= 'A' && c <= 'F'){
            return c - 'A' + 10;
        }
    };

    std::string res;
    res.resize(hex_key.size() / 2);
    for(int i = 0; i < res.size(); i++) {
        int hi = transf(hex_key[2 * i]);
        int lo = transf(hex_key[2 * i + 1]);
        res[i] = static_cast<char>((hi << 4) | lo);
    }
    return res;
}

std::tuple<std::string, std::string>GenECDHKey() {
    EC_GROUP* curve = EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1);
    if(!curve) {
        throw std::runtime_error("EC_GROUP_new_by_curve_name failed");
    }

     EC_KEY* key = EC_KEY_new();
     if(!key || EC_KEY_set_group(key, curve) != 1 || EC_KEY_generate_key(key) != 1) {
        EC_KEY_free(key);
        EC_GROUP_free(curve);
        throw std::runtime_error("generate EC key failed");
     }

     const BIGNUM* priv_bn = EC_KEY_get0_private_key(key);
     std::string pri(32, '\0');
     if(BN_bn2binpad(priv_bn, reinterpret_cast<unsigned char*>(&pri[0]),32) <= 0) {
        EC_KEY_free(key);
        EC_GROUP_free(curve);
        throw std::runtime_error("export private key failed");
     }

     const EC_POINT* pub_point = EC_KEY_get0_public_key(key);
     size_t pub_len = EC_POINT_point2oct(curve, pub_point, POINT_CONVERSION_UNCOMPRESSED, nullptr, 0, nullptr);

     std::string pub(pub_len, '\0');
     if(EC_POINT_point2oct(curve, pub_point, POINT_CONVERSION_UNCOMPRESSED,
                            reinterpret_cast<unsigned char*>(&pub[0]), pub_len, nullptr) != pub_len) {
        EC_KEY_free(key);
        EC_GROUP_free(curve);
        throw std::runtime_error("export public key failed");
    }

    EC_KEY_free(key);
    EC_GROUP_free(curve);
    return {pri, pub};
}

std::string GenECDHSharedSecret(const std::string& client_pri, const std::string& server_pub) {
    EC_GROUP* curve = EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1);
    if(!curve) {
        throw std::runtime_error("EC_GROUP_new_by_curve_name failed");
    }

    EC_KEY* my_key = EC_KEY_new();
    BIGNUM* priv_bn = BN_bin2bn(reinterpret_cast<const unsigned char*>(client_pri.data()),
                                static_cast<int>(client_pri.size()), nullptr);
    if(!my_key || !priv_bn || EC_KEY_set_group(my_key, curve) != 1 ||
        EC_KEY_set_private_key(my_key, priv_bn) != 1) {
        BN_free(priv_bn);
        EC_KEY_free(my_key);
        EC_GROUP_free(curve);
        throw std::runtime_error("set private key failed");
    }
    BN_free(priv_bn);

    EC_POINT* peer_pub = EC_POINT_new(curve);
    if (!peer_pub ||
        EC_POINT_oct2point(curve, peer_pub,
                           reinterpret_cast<const unsigned char*>(server_pub.data()),
                           server_pub.size(), nullptr) != 1) {
        EC_POINT_free(peer_pub);
        EC_KEY_free(my_key);
        EC_GROUP_free(curve);
      throw std::runtime_error("parse peer public key failed");
    }

    unsigned char secret[32];
    int secret_len = ECDH_compute_key(secret, sizeof(secret), peer_pub, my_key, nullptr);
    if (secret_len <= 0) {
        EC_POINT_free(peer_pub);
        EC_KEY_free(my_key);
        EC_GROUP_free(curve);
        throw std::runtime_error("ECDH_compute_key failed");
    }
    std::string shared(reinterpret_cast<char*>(secret), secret_len);
    EC_POINT_free(peer_pub);
    EC_KEY_free(my_key);
    EC_GROUP_free(curve);
    return shared;
}

}  // namespace crypt
}  // namespace logger