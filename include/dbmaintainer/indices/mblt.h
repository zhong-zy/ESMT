
#ifndef SHRUBS_LIKE_INDEX_MBLT_H
#define SHRUBS_LIKE_INDEX_MBLT_H

#include <iostream>
#include <vector>
#include <list>
#include <string>
#include <strstream>
#include <algorithm>
#include <memory>

#include "cpputils/sha256ssl.h"
#include "dbmaintainer/indices/DataStore/MemStore.h"
#include "dbmaintainer/indices/DataStore/RocksDBStore.h"
#include <shared_mutex>

inline std::string key2str(const uint64_t key) {
    return std::to_string(key);
}

typedef uint64_t KeyType;
typedef MemData DataItem;

class MerkleBLinkTree {
private:

    struct Node {
        bool is_root = false;
        bool is_leaf = false;
        int level=-1;
        std::shared_ptr<Node> parent_ = nullptr;
        std::vector<KeyType> keys;

        int8_t state=1, hash_idx=0;
        std::string data_;
        std::string hash_[2];
        std::shared_mutex hash_mtx;

        std::string& hash() {
            return hash_[hash_idx];
        }

        void update_hash() {
            if (state == 1) {
                hash() = compute_hash();
            } else if (state == 2) {
                hash_idx ^= 1;
            }
            state = 0;
        }

        virtual ~Node() = default;
        virtual std::string compute_hash() = 0;

    };

    struct NonLeafNode : public Node {
        std::vector<std::shared_ptr<Node>> children;
        std::shared_ptr<NonLeafNode> next = nullptr;

        NonLeafNode() {
            this->is_leaf = false;
        }

        std::string compute_hash() override {
            std::string concat;
            concat.reserve(32 * children.size());
            for (const auto &item: children) {
                concat.append(item->hash());
            }

            return cpputils::HashFunction(concat);
        }

    };

    struct LeafNode : public Node {
        std::vector<DataItem> children;
        std::shared_ptr<LeafNode> next = nullptr;

        LeafNode() {
            this->is_leaf = true;
        }

        std::string compute_hash() override {
            std::string concat;
            concat.reserve(32 * children.size());
            for(auto& item : children) {
                concat.append(item.hash());
            }

            return cpputils::HashFunction(concat);
        }

    };

    std::shared_ptr<Node> root_;
    const int order_;

    std::vector<std::shared_ptr<std::thread>> pre_hash_thread_pool;
    int thread_num = 10;
    cpputils::Processor<std::shared_ptr<DataItem>> pre_hash_queue;
public:
    MerkleBLinkTree(int order=2) : order_(order), root_(nullptr) {

    }

    void build(std::vector<KeyType>::iterator st, std::vector<KeyType>::iterator ed) {
        auto size = ed - st;

        std::sort(st, ed);
        std::vector<std::shared_ptr<Node>> leaves = create_leaves(st, ed);

        root_ = build_tree(leaves);

    }

private:

    std::vector<std::shared_ptr<Node>> create_leaves(std::vector<KeyType>::iterator st, std::vector<KeyType>::iterator ed) {
        std::vector<std::shared_ptr<Node>> leaves;
        std::shared_ptr<LeafNode> prev;
        size_t size = ed - st;

        for (size_t i = 0; i < size; i += order_) {
            auto leaf = std::make_shared<LeafNode>();
            size_t end = std::min(i + order_, size);

            for (size_t j = i; j < end; ++j) {
                auto key = *(st+j);
                std::string value = "";
                leaf->keys.push_back(key);
                leaf->children.emplace_back(key, value);

                leaf->children.back().update_hash();
            }
            leaf->update_hash();
            leaf->level = 0;

            if (prev) prev->next = leaf;
            prev = leaf;
            leaves.push_back(leaf);
        }
        return leaves;
    }

    std::shared_ptr<Node>
    build_tree(const std::vector<std::shared_ptr<Node>>& children) {
        if (children.size() == 1) {

            auto root = children[0];
            root->is_root = true;
            return children[0];
        }

        std::vector<std::shared_ptr<Node>> current_level;
        std::shared_ptr<NonLeafNode> prev;

        for (size_t i = 0; i < children.size(); i += order_) {
            auto parent = std::make_shared<NonLeafNode>();
            size_t end = std::min(i + order_, children.size());

            for (size_t j = i; j < end; ++j) {
                parent->children.push_back(children[j]);

                children[j]->parent_ = parent;

                parent->keys.push_back(children[j]->keys.back());
            }

            parent->update_hash();
            parent->level = parent->children[0]->level + 1;

            if (prev) prev->next = parent;
            prev = parent;
            current_level.push_back(parent);
        }
        return build_tree(current_level);
    }

public:

    void insert(const KeyType &key, const DataType &data, bool cal_hash=true, bool pre_hash_data = false) {
        if(!root_) {
            root_ = std::make_shared<LeafNode>();

            root_->is_root = true;
        }
        std::shared_ptr<Node> current = root_;

        while (!current->is_leaf) {
            auto it = std::lower_bound(current->keys.begin(), current->keys.end(), key);

            if (it == current->keys.end()) {
                std::cout << "insert: non-leaf node out of range (not handled yet)" << std::endl;
                exit(-1);
            }
            current = std::dynamic_pointer_cast<NonLeafNode>(current)->children[it - current->keys.begin()];
        }

        auto current_leaf = std::dynamic_pointer_cast<LeafNode>(current);
        auto insert_pos = std::lower_bound(current_leaf->keys.begin(), current_leaf->keys.end(), key);
        auto offset = insert_pos - current_leaf->keys.begin();

        if (*insert_pos == key) {
            current_leaf->children[offset].update(data);
        } else {
            current_leaf->keys.insert(insert_pos, key);
            current_leaf->children.insert(current_leaf->children.begin() + offset, {key, data});
        }

        if(cal_hash) {

            current_leaf->state = 1;
            current_leaf->children[offset].update_hash();

            update_hash_from(current_leaf);
        }
        if (pre_hash_data) {

        }
    }

    bool search(KeyType key, DataType &ret) {
        std::shared_ptr<LeafNode> current_leaf = std::dynamic_pointer_cast<LeafNode>(find_leaf(key));
        if (!current_leaf || !current_leaf->is_leaf) {
            return false;
        }
        auto data_item = searchLeaf(key, current_leaf);
        ret = data_item->data();
        return data_item->state == 0;
    }

    struct MerkleProof {
        enum Type {
            LEFT,
            RIGHT
        };
        struct Item {
            int8_t type;
            std::string hash;
        };
        std::list<Item> hashes;
    };

    std::shared_ptr<MerkleProof> prove(const KeyType &key) {
        auto proof = std::make_shared<MerkleProof>();
        std::shared_ptr<Node> current = root_;
        while (!current->is_leaf) {
            auto idx = std::lower_bound(current->keys.begin(), current->keys.end(), key) - current->keys.begin();
            auto current_nl = std::dynamic_pointer_cast<NonLeafNode>(current);
            int i = 0;
            for (; i < idx; ++i) {
                proof->hashes.push_front({MerkleProof::LEFT, current_nl->children[i]->hash()});
            }
            ++i;
            for (; i < current_nl->children.size() ; ++i) {
                proof->hashes.push_front({MerkleProof::RIGHT, current_nl->children[i]->hash()});
            }
            current = current_nl->children[idx];
        }
        auto idx = std::lower_bound(current->keys.begin(), current->keys.end(), key) - current->keys.begin();
        auto current_leaf = std::dynamic_pointer_cast<LeafNode>(current);
        int i = 0;
        for (; i < idx; ++i) {
            proof->hashes.push_front({MerkleProof::LEFT, current_leaf->children[i].hash()});
        }
        ++i;
        for (; i < current_leaf->children.size() ; ++i) {
            proof->hashes.push_front({MerkleProof::RIGHT, current_leaf->children[i].hash()});
        }
        return proof;
    }

    std::string getRootHash() {
        return root_->hash();
    }

    void lazyPointUpdate(const KeyType& key, bool force=false) {
        auto node = find_leaf(key);
        auto leaf = std::dynamic_pointer_cast<LeafNode>(node);
        auto data_item = searchLeaf(key, leaf);
        if (force || data_item->state == 1) {
            data_item->update_hash();
            leaf->state = 1;
            update_hash_from(leaf);
        }
    }

private:

    std::shared_ptr<Node> find_leaf(const KeyType& key) const {
        if(!root_) {
            std::cout << "no root" << std::endl;
            exit(-1);
        }
        std::shared_ptr<Node> current = root_;

        while (!current->is_leaf) {
            auto it = std::lower_bound(current->keys.begin(), current->keys.end(), key);
            if (it == current->keys.end()) {
                std::cout << "find_leaf: out of range" << std::endl;
                exit(-1);
            }
            current = std::dynamic_pointer_cast<NonLeafNode>(current)->children[it - current->keys.begin()];
        }
        if (!current->is_leaf) {
            std::cout << "find_leaf: leaf node not found?" << std::endl;
            exit(-1);
        }
        return current;
    }

    static DataItem* searchLeaf(KeyType key, std::shared_ptr<LeafNode> &current_leaf) {
        auto data_pos = std::lower_bound(current_leaf->keys.begin(), current_leaf->keys.end(), key);
        auto offset = data_pos - current_leaf->keys.begin();
        if (data_pos == current_leaf->keys.end()) {
            std::cout << "search leaf: not found" << std::endl;
            exit(-1);
        }
        auto data_item = &current_leaf->children[offset];
        return data_item;
    }

    void update_hash_from(std::shared_ptr<Node> node) {
        while(node) {

            if (node->parent_) {
                node->parent_->state = 1;
            }
            node->update_hash();

            node = node->parent_;
        }
    }

    void split_child(NonLeafNode* parent, int child_idx) {

    }

};

#endif
