
#ifndef SHRUBS_LIKE_INDEX_MERKLE_BUCKET_TREE_H
#define SHRUBS_LIKE_INDEX_MERKLE_BUCKET_TREE_H

#include "dbmaintainer/mbt/mbt_base.h"
#include "config.h"

static size_t HashForBucket(const std::string &key) {
    return std::hash<std::string>()(key);
}

static size_t HashForBucket(const uint64_t &key) {
    return std::hash<uint64_t>()(key);
}

class MerkleBucketTree: public MBT_Base {
public:
    MerkleBucketTree(int fanout=2, int capacity=65536): MBT_Base(fanout), capacity_(capacity) {

        if (config::mbt_bucket_capacity != 65536) capacity_ = config::mbt_bucket_capacity;
    }
private:
    int capacity_;

    std::vector<std::shared_ptr<Node>> create_leaves(std::vector<KeyType>::iterator st, std::vector<KeyType>::iterator ed) {
        std::vector<std::shared_ptr<Node>> buckets;

        std::cout << "[MBT] build keys=" << (int64_t)(ed - st) << " buckets=" << capacity_ << std::endl;

        buckets.resize(capacity_);
        for (int i = 0; i < capacity_; ++i) {
            auto bucket = std::make_shared<LeafBucketNode>();
            bucket->bucket_id = i;
            bucket->type_ = BUCKET;
            bucket->level_ = 0;
            buckets[i] = bucket;
        }
        for (auto it = st; it != ed; ++it) {
            auto key = *it;
            auto hash = HashForBucket(key);
            auto index = hash % capacity_;

            std::shared_ptr<LeafBucketNode> bucket = std::dynamic_pointer_cast<LeafBucketNode>(buckets[index]);
            bucket->keys.push_back(key);
            bucket->datas.push_back({key, ""});
            bucket->datas.back().update_hash();
        }

        return buckets;
    }

public:

    void insert(const KeyType &key, const DataType &value, bool cal_hash=false) {
        auto hash = HashForBucket(key);
        auto index = hash % capacity_;
        auto leaf = find_leaf(root_, index);
        if (leaf == nullptr) {
            std::cout << "[MerkleBucketTree] insert in nullptr bucket (tree does not init?)" << std::endl;
            throw "[MerkleBucketTree] insert in nullptr bucket (tree does not init?)";
        }
        auto bucket = std::dynamic_pointer_cast<LeafBucketNode>(leaf);

        auto it = std::lower_bound(bucket->keys.begin(), bucket->keys.end(), key);
        auto idx = it - bucket->keys.begin();
        if ((*it) == key) {
            bucket->datas[idx].update(value);
        } else {
            bucket->keys.insert(it, key);
            bucket->datas.insert(bucket->datas.begin()+idx, {key, value});
        }
    }

    std::tuple<std::string, bool> Get(const KeyType &key) {
        auto hash = HashForBucket(key);
        auto index = hash % capacity_;
        auto leaf = find_leaf(root_, index);
        if (leaf == nullptr) {
            return {"", false};
        }

        auto bucket = std::dynamic_pointer_cast<LeafBucketNode>(leaf);
        auto it = std::lower_bound(bucket->keys.begin(), bucket->keys.end(), key);
        auto idx = it - bucket->keys.begin();
        if ((*it) == key) {
            return {bucket->datas[idx].data(), true};
        } else {
            return {"", false};
        }
    }

    std::shared_ptr<std::vector<std::shared_ptr<Node>>> findPath(const std::shared_ptr<Node> &current_root, const uint64_t &key) {
        auto hash = HashForBucket(key);
        auto index = hash % capacity_;
        return find_path_base(current_root, index);
    }

    std::shared_ptr<MerkleProof> prove(const std::shared_ptr<Node> &current_root, const uint64_t &key) {
        auto hash = HashForBucket(key);
        auto index = hash % capacity_;
        return prove_base(current_root, index);
    }
};

#endif
