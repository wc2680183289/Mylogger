#include "aes_crypt.h"

#include <openssl/aes.h>
#include <openssl/rand.h>

#include <cstring>
#include <stdexcept>
#include <vector>

namespace logger {
namespace crypt {

constexpr int kKeyBits = 128;
constexpr int kKeyLen = 16;
constexpr int kIvLen = AES_BLOCK_SIZE;

std::string Pkcs7Pad(const void* input, size_t input_size) {
    size_t pad = kIvLen - (input_size % kIvLen);
    if(pad == 0) {
        pad = kIvLen;
    }
    std::string out(static_cast<const char*>(input), input_size);
    out.append(pad, static_cast<char>(pad));
    return out;
}

std::string Pkcs7Unpad(const std::string& data) {
    if(data.empty()) {
        throw std::runtime_error("empty decrypt result");
    }
    unsigned char pad = static_cast<unsigned char>(data.back());
    if(pad == 0 || pad > kIvLen || pad > data.size()) {
        throw std::runtime_error("bad pkcs7 padding");
    }
    return data.substr(0, data.size() - pad);
}

std::string ToAesKey(std::string key) {
    if(key.size() == 32) {
        key = HexKeyToBinary(key);
    }
    if(key.size() < static_cast<size_t>(kKeyLen)) {
        throw std::invalid_argument("key too short");
    }
    return key.substr(0, kKeyLen);
}

AESCrypt::AESCrypt(std::string key) {
    key_ = ToAesKey(std::move(key));

    iv_.assign(kIvLen, '\0');
}

std::string AESCrypt::GenerateKey() {
    unsigned char buf[kKeyLen];
    if(RAND_bytes(buf, kKeyLen) != 1) {
        throw std::runtime_error("RAND_bytes failed");
    }
    return BinaryKeyToHex(std::string(reinterpret_cast<char*>(buf), kKeyLen));
}

std::string AESCrypt::GenerateIV() {
    unsigned char buf[kIvLen];
    if(RAND_bytes(buf, kIvLen) != 1) {
        throw std::runtime_error("RAND_bytes failed");
    }
    return BinaryKeyToHex(std::string(reinterpret_cast<char*>(buf), kKeyLen));
}

void AESCrypt::Encrypt(const void* input, size_t input_size, std::string& output) {
    std::string plain = Pkcs7Pad(input, input_size);

    AES_KEY encrypt_key;
    if(AES_set_encrypt_key(reinterpret_cast<const unsigned char*>(key_.data()), kKeyBits, &encrypt_key) != 0) {
        throw std::runtime_error("AES_set_encrypt_key failed");
    }

    unsigned char iv_buf[kIvLen];
    std::memcpy(iv_buf, iv_.data(), kIvLen);

    std::vector<unsigned char>cipher(plain.size());

    AES_cbc_encrypt(reinterpret_cast<const unsigned char*>(plain.data()),
                  cipher.data(),
                  plain.size(),
                  &encrypt_key,
                  iv_buf,
                  AES_ENCRYPT);
    output.assign(reinterpret_cast<char*>(cipher.data()), cipher.size());
}

std::string AESCrypt::Decrypt(const void* data, size_t size) {
    if(size == 0 || size % kIvLen != 0) {
        throw std::runtime_error("ciphertext size invalid");
    }

    AES_KEY decrypt_key;
    if(AES_set_decrypt_key(reinterpret_cast<const unsigned char*>(key_.data()), kKeyBits, &decrypt_key) != 0) {
        throw std::runtime_error("AES_set_decrypt_key failed");
    }

    unsigned char iv_buf[kIvLen];
    std::memcpy(iv_buf, iv_.data(), kIvLen);

    std::vector<unsigned char> plain(size);
    AES_cbc_encrypt(reinterpret_cast<const unsigned char*>(data),
                  plain.data(),
                  size,
                  &decrypt_key,
                  iv_buf,
                  AES_DECRYPT);
    std::string padded(reinterpret_cast<char*>(plain.data()), size);
    return Pkcs7Unpad(padded);

}

}
}