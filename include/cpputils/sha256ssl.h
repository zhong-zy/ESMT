
#ifndef SHRUBS_LIKE_INDEX_SHA256SSL_H
#define SHRUBS_LIKE_INDEX_SHA256SSL_H

#include <openssl/evp.h>
#include <openssl/sha.h>
#include <iostream>
#include <vector>
#include <cstdint>

namespace cpputils {
    static std::string HashFunction(const std::string &input) {
        EVP_MD_CTX *ctx = EVP_MD_CTX_new();

        EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);

        EVP_DigestUpdate(ctx, input.c_str(), input.size());

        unsigned char hash[EVP_MAX_MD_SIZE];
        unsigned int hash_len = 0;

        EVP_DigestFinal_ex(ctx, hash, &hash_len);

        EVP_MD_CTX_free(ctx);

        return std::string(reinterpret_cast<char *>(hash), hash_len);
    }

    static std::vector<uint8_t> HashFunction(const std::vector<uint8_t> &input) {
        EVP_MD_CTX *ctx = EVP_MD_CTX_new();

        EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);

        EVP_DigestUpdate(ctx, input.data(), input.size());

        unsigned char hash[EVP_MAX_MD_SIZE];
        unsigned int hash_len = 0;

        EVP_DigestFinal_ex(ctx, hash, &hash_len);

        EVP_MD_CTX_free(ctx);

        return {reinterpret_cast<uint8_t *>(hash), reinterpret_cast<uint8_t *>(hash) + hash_len};
    }

}

#endif
