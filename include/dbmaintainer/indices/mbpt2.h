
#ifndef SHRUBS_LIKE_INDEX_MBPT_H
#define SHRUBS_LIKE_INDEX_MBPT_H

#include <iostream>
#include <vector>
#include <string>
#include <strstream>
#include <algorithm>
#include <memory>
#include "cpputils/SHA256.h"

typedef uint64_t KeyType;

std::string key2str(const KeyType key) {
    return std::to_string(key);
}

std::string sha256(const std::string &str) {
    cpputils::SHA256 sha;
    sha.update(str);
    return cpputils::SHA256::toString(sha.digest());
}

class MerkleBPlusTree {
private:

    struct Node {
        bool is_root;
        bool is_leaf;
        std::shared_ptr<Node> parent;

        struct Item {
            KeyType key;
            std::string hash;
            int8_t state;
            union child {
                std::shared_ptr<Node> child_ptr;
                std::string data;
                child(){}
                ~child(){}
            } child;
            Item(KeyType &key, std::string &hash, int8_t state, std::string &data): key(key), hash(hash), state(state){
                child.data = data;
            }
        };
        std::vector<Item> items;

        void add_leaf_item(KeyType &key, std::string &data) {
            std::string hash = sha256(key2str(key) + "|" + data);
            Node::Item item{key, hash, 0, data};

            items.push_back({key, hash, 0, data});
        }
    };

    std::shared_ptr<Node> root_;
    const int order_;

public:
    MerkleBPlusTree(int order=2) : order_(order), root_(nullptr) {}

    void build(std::vector<KeyType>::iterator st, std::vector<KeyType>::iterator ed) {
        auto size = ed - st;

        std::sort(st, ed);
        std::vector<std::shared_ptr<Node>> leaves = create_leaves(st, ed);

        root_ = build_tree(leaves);
        root_->update_hash();
    }

private:

    std::vector<std::shared_ptr<Node>> create_leaves(std::vector<KeyType>::iterator st, std::vector<KeyType>::iterator ed) {
        std::vector<std::shared_ptr<Node>> leaves;
        std::shared_ptr<Node> prev;
        size_t size = ed - st;

        for (size_t i = 0; i < size; i += order_) {
            auto leaf = std::make_shared<Node>();
            leaf->is_leaf = true;
            size_t end = std::min(i + order_, size);

            for (size_t j = i; j < end; ++j) {
                auto key = *(st+i);
                std::string value = "";
                leaf->keys.push_back(key);
                leaf->data.push_back(value);
                leaf->data_hashes.push_back(sha256(key2str(key) + value));
            }

            if (prev) prev->next = leaf;
            prev = leaf;
            leaves.push_back(leaf);
        }
        return leaves;
    }

    std::shared_ptr<Node>
    build_tree(const std::vector<std::shared_ptr<Node>>& children) {

    }

public:

    void update(const KeyType& key, const std::string& data, bool cal_hash=true) {

    }

    void insert(const KeyType& key, const std::string& data, bool cal_hash=true) {
        if(!root_) {
            root_ = std::make_shared<Node>();

            root_->is_root = true;
        }
        std::shared_ptr<Node> current = root_;

        while (!current->is_leaf) {
            auto it = std::upper_bound(current->keys.begin(), current->keys.end(), key);
            current = std::dynamic_pointer_cast<NonNode>(current)->children[it - current->keys.begin()];
        }

        auto current_leaf = std::dynamic_pointer_cast<Node>(current);
        auto insert_pos = std::lower_bound(current_leaf->keys.begin(), current_leaf->keys.end(), key);

        if (*insert_pos == key) {

        } else {
            current_leaf->keys.insert(insert_pos, key);
            current_leaf->data.insert(current_leaf->data.begin() + (insert_pos - current_leaf->keys.begin()), data);
        }

        if(cal_hash) {
            update_hash(key);
        }
    }

    void update_hash(const KeyType& key) {
        auto leaf = find_leaf(key);
        update_hash_from(leaf);
    }

private:

    void update_hash_from(std::shared_ptr<Node> node) {
        while(node) {
            std::string new_hash = node->compute_hash();
            if(auto nl = dynamic_cast<NonNode*>(node)) {
                size_t idx = get_parent_index(node);
                if(idx != -1) {
                    nl->parent->child_hashes[idx] = new_hash;
                    nl->parent->states[idx] = 0;
                }
            }
            node = node->parent;
        }
    }

    void split_child(NonNode* parent, int child_idx) {

    }

    std::shared_ptr<Node> find_leaf(const KeyType& key) const {  }
    size_t get_parent_index(std::shared_ptr<Node> child) const {  }
};

#endif
