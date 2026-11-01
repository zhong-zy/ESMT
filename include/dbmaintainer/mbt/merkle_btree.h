
#ifndef SHRUBS_LIKE_INDEX_MERKLE_BTREE_H
#define SHRUBS_LIKE_INDEX_MERKLE_BTREE_H

#include "dbmaintainer/mbt/mbt_base.h"

class MerkleBTree: public MBT_Base {
public:
    MerkleBTree(int fanout = 2) : MBT_Base(fanout) {

    }

private:
    std::vector<std::shared_ptr<Node>> create_leaves(std::vector<KeyType>::iterator st, std::vector<KeyType>::iterator ed) {
        std::vector<std::shared_ptr<Node>> leaves;
        size_t size = ed - st;

        for (size_t i = 0; i < size; i += fanout_) {
            auto leaf = std::make_shared<LeafBucketNode>();
            size_t end = std::min(i + fanout_, size);

            for (size_t j = i; j < end; ++j) {
                auto key = *(st+j);
                std::string value = "";
                leaf->keys.push_back(key);
                leaf->datas.emplace_back(key, value);
                leaf->datas.back().update_hash();
            }

            leaf->level_ = 0;
            leaf->type_ = LEAF;
            leaf->datas.back().update_hash();
            leaves.push_back(leaf);
        }
        return leaves;
    }

public:

    void insert(const KeyType &key, const DataType &value, bool cal_hash=false) {
        auto index = key;
        auto leaf = find_leaf(root_, index);
        if (leaf == nullptr) {
            std::cout << "[MerkleBTree] insert in nullptr leaf (tree does not init?)" << std::endl;
            throw "[MerkleBTree] insert in nullptr leaf (tree does not init?)";
        }
        auto bucket = std::dynamic_pointer_cast<LeafBucketNode>(leaf);

        auto it = std::lower_bound(bucket->keys.begin(), bucket->keys.end(), key);
        auto idx = it - bucket->keys.begin();
        if ((*it) == key) {
            bucket->datas[idx].update(value);
        } else {

            std::cout << "[MerkleBTree][insert] insert new key (not supported)";
            throw  "[MerkleBTree][insert] insert new key (not supported)";
        }
    }

    std::tuple<std::string, bool> Get(const KeyType &key) {
        auto index = key;
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
        auto index = key;
        return find_path_base(current_root, index);
    }

    std::shared_ptr<MerkleProof> prove(const std::shared_ptr<Node> &current_root, const uint64_t &key) {
        auto index = key;
        return prove_base(current_root, index);
    }
};

#endif
