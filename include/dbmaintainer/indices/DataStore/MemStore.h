
#ifndef SHRUBS_LIKE_INDEX_MEMSTORE_H
#define SHRUBS_LIKE_INDEX_MEMSTORE_H

#include <iostream>
#include <string>
#include <shared_mutex>
#include "cpputils/SHA256.h"

typedef std::string DataType;
struct MemData {
    int8_t state=0, hash_idx=0;

    DataType data_;
    std::string hash_[2];

    std::shared_ptr<std::shared_mutex> hash_mtx;
    std::shared_ptr<std::shared_mutex> state_mtx;

    MemData() = default;

    MemData(const KeyType &key, const DataType &data) {

        data_ = data;
        state = 1;
        hash_mtx = std::make_shared<std::shared_mutex>();
        state_mtx = std::make_shared<std::shared_mutex>();
    }

    void update(const DataType &data) {
        data_ = data;
        state = 1;
    }

    void update_hash() {
        state_mtx->lock();
        hash_mtx->lock();
        if (state == 1) {
            hash() = compute_hash();
        } else if (state == 2) {
            hash_idx ^= 1;
        }
        state = 0;
        hash_mtx->unlock();
        state_mtx->unlock();
    }
    void pre_update_hash() {
        state_mtx->lock_shared();
        if (state == 1) {
            hash_[hash_idx^1] = compute_hash();
        }
        state_mtx->unlock_shared();
    }
    std::string data() const {
        return data_;
    }
    std::string& hash() {
        return hash_[hash_idx];
    }
    std::string compute_hash() {
        return sha256(data_);
    }
};

#endif
