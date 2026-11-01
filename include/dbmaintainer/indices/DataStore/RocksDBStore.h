
#ifndef SHRUBS_LIKE_INDEX_RocksDBStore_H
#define SHRUBS_LIKE_INDEX_RocksDBStore_H

#include <iostream>
#include <string>
#include "cpputils/SHA256.h"
#include "dbmaintainer/indices/ADSStore/ADSStoreRocksDB.h"

typedef std::string DataType;
static std::shared_ptr<ADSStoreRocksDB> data_db;

struct RocksDBData {
    int8_t state=0, hash_idx=0;
    KeyType key_;

    std::string hash_[2];

    std::shared_ptr<std::shared_mutex> hash_mtx;
    std::shared_ptr<std::shared_mutex> state_mtx;

    RocksDBData(const KeyType &key, const DataType &data) {
        if (data_db == nullptr) data_db = std::make_shared<ADSStoreRocksDB>("./data_db");
        key_ = key;

        data_db->Put(0, std::to_string(key_), data);
        state = 1;
        hash_mtx = std::make_shared<std::shared_mutex>();
        state_mtx = std::make_shared<std::shared_mutex>();
    }

    void update(const DataType &data) {

        data_db->Put(0, std::to_string(key_), data);
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
        auto data =  data_db->Get(0, std::to_string(key_));
        return *data;
    }
    std::string& hash() {
        return hash_[hash_idx];
    }
    std::string compute_hash() {
        auto data =  data_db->Get(0, std::to_string(key_));
        return sha256(*data);
    }
};

#endif
