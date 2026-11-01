
#ifndef SHRUBS_LIKE_INDEX_MBT_H
#define SHRUBS_LIKE_INDEX_MBT_H

#include <memory>
#include <vector>
#include <string>
#include <algorithm>
#include "cpputils/sha256ssl.h"
typedef uint64_t KeyType;
#include "dbmaintainer/indices/DataStore/MemStore.h"

size_t HashForBucket(const std::string &key) {
    return std::hash<std::string>()(key);
}

size_t HashForBucket(const uint64_t &key) {
    return std::hash<uint64_t>()(key);
}

typedef MemData DataItem;
class MerkleBucketTree {
private:
    class Node {
        std::shared_ptr<MerkleBucketTree> tree;
        std::shared_ptr<Node> parent;
        std::vector<std::shared_ptr<Node>> children;
        bool is_leaf_;
        bool dup_;
        std::string hash_;
        bool sort_;
    };

    struct Bucket {
        std::vector<KeyType> keys;
        std::vector<DataItem> datas;
    };

private:
    std::shared_ptr<Node> root_;
    std::string merkle_root_;
    std::vector<std::shared_ptr<Node>> leafs;
    bool sort_;
    int fanout_;

    std::vector<std::shared_ptr<Bucket>> buckets_;
    int capacity_;
public:
    void build(std::vector<KeyType>::iterator st, std::vector<KeyType>::iterator ed) {
        buckets_.resize(capacity_);
        for (auto it = st; it != ed; ++it) {
            auto key = *it;
            auto hash = HashForBucket(key);
            auto index = hash % capacity_;
            auto& bucket = buckets_[index];
            if (bucket == nullptr) {
                bucket = std::make_shared<Bucket>();
            }
            bucket->keys.push_back(key);
            bucket->datas.push_back({key, ""});
        }
    }

    void insert(const KeyType &key, const DataType &value) {
        auto hash = HashForBucket(key);
        auto index = hash % capacity_;
        auto& bucket = buckets_[index];
        if (bucket == nullptr) {
            bucket = std::make_shared<Bucket>();

            bucket->keys.push_back(key);
            bucket->datas.push_back({key, value});
            return;
        }

        auto it = std::lower_bound(bucket->keys.begin(), bucket->keys.end(), key);
        auto idx = it - bucket->keys.begin();
        if ((*it) == key) {
            bucket->datas[idx].update(value);
        } else {
            bucket->keys.insert(it, key);
            bucket->datas.insert(bucket->datas.begin()+idx, {key, value});
        }
    }

    std::tuple<std::string, bool> Get(KeyType &key) {
        auto hash = HashForBucket(key);
        auto index = hash % capacity_;
        auto& bucket = buckets_[index];

        if (bucket == nullptr) {
            return {"", false};
        }

        auto it = std::lower_bound(bucket->keys.begin(), bucket->keys.end(), key);
        auto idx = it - bucket->keys.begin();
        if ((*it) == key) {
            return {bucket->datas[idx].data(), true};
        } else {
            return {"", false};
        }
    }
};

#endif
