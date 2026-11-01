
#ifndef INDEX_MAINTAIN_HASH_H
#define INDEX_MAINTAIN_HASH_H

#include <iostream>

namespace cpputils {
    const uint64_t kFNVOffsetBasis64 = 0xCBF29CE484222325;
    const uint64_t kFNVPrime64 = 1099511628211;

    inline uint64_t Hash(uint64_t val) {
        uint64_t hash = kFNVOffsetBasis64;

        for (int i = 0; i < 8; i++) {
            uint64_t octet = val & 0x00ff;
            val = val >> 8;

            hash = hash ^ octet;
            hash = hash * kFNVPrime64;
        }
        return hash;
    }
}

#endif
