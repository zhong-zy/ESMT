
#ifndef SHRUBS_LIKE_INDEX_MBT_BASE_H
#define SHRUBS_LIKE_INDEX_MBT_BASE_H

#include <memory>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include <list>
#include <set>
#include <queue>
#include "cpputils/sha256ssl.h"
typedef uint64_t KeyType;
#include "dbmaintainer/indices/DataStore/MemStore.h"

typedef MemData DataItem;
class MBT_Base {

public:
    enum NodeType {
        NON_LEAF,
        LEAF,
        BUCKET,
    };

    class Node {
    public:
        virtual ~Node() = default;
        int level_ = -1;
        virtual NodeType type()=0;
        virtual uint64_t navigation_key()=0;
        virtual std::string Hash()=0;
        virtual void computeHash()=0;
        virtual void copy()=0;

        bool operator<(Node &b) {
            return this->level_ < b.level_;
        }
        bool operator>(Node &b) {
            return this->level_ > b.level_;
        }

        friend bool operator<(std::shared_ptr<Node> &a, std::shared_ptr<Node> &b) {
            return a->level_ < b->level_;
        }
        friend bool operator>(std::shared_ptr<Node> &a, std::shared_ptr<Node> &b) {
            return a->level_ > b->level_;
        }
    };

    struct NonLeafNode: public Node {
        std::vector<KeyType> keys;
        std::vector<std::shared_ptr<Node>> children;
        std::string hash_;

        void copy() {
            auto new_node = std::make_shared<NonLeafNode>();
            new_node->keys = keys;
            new_node->children = children;
            new_node->hash_ = hash_;
            new_node->level_ = level_;
        }
        NodeType type() {
            return NON_LEAF;
        }
        uint64_t navigation_key() {

            return keys.back();
        }
        std::string Hash() {
            return hash_;
        }
        void computeHash() {
            hash_ = cpputils::HashFunction(hash_data_lazy());

        }
        std::string hash_data_lazy() {
            std::stringstream row_data;
            for (int i = 0; i < keys.size(); ++i) {
                row_data << keys[i] << "|" << children[i]->Hash() << "|";
            }
            return row_data.str();
        }
    };

    struct LeafBucketNode: public Node {
        int64_t bucket_id=-1;
        std::vector<KeyType> keys;
        std::vector<DataItem> datas;
        std::string hash_;
        NodeType type_;

        void copy() {
            auto new_node = std::make_shared<LeafBucketNode>();
            new_node->bucket_id = bucket_id;
            new_node->keys = keys;
            new_node->datas = datas;
            new_node->hash_ = hash_;
            new_node->level_ = level_;
            new_node->type_ = type_;
        }
        NodeType type() {
            return type_;
        }
        uint64_t navigation_key() {
            if (type_ == BUCKET) {
                return bucket_id;
            } else {
                return keys.back();
            }
        }
        std::string Hash() {
            return hash_;
        }
        void computeHash() {
            hash_ = cpputils::HashFunction(hash_data_lazy());

        }
        std::string hash_data_lazy() {
            std::stringstream row_data;
            row_data << bucket_id << "|";
            for (int i = 0; i < keys.size(); ++i) {
                row_data << keys[i] << "|" << datas[i].hash() << "|";
            }
            return row_data.str();
        }
    };

protected:

    int fanout_;

public:
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

    std::shared_ptr<Node> root_;

    MBT_Base(int fanout=2): fanout_(fanout) {
    }

    std::string getRootHash() {
        return root_->Hash();
    }

    void build(std::vector<KeyType>::iterator st, std::vector<KeyType>::iterator ed) {
        std::sort(st, ed, std::less<>());
        auto buckets = create_leaves(st, ed);
        root_ = build_tree(buckets);
    }
protected:
    virtual std::vector<std::shared_ptr<Node>> create_leaves(std::vector<KeyType>::iterator st, std::vector<KeyType>::iterator ed)=0;

    std::shared_ptr<Node>
    build_tree(const std::vector<std::shared_ptr<Node>>& children) {
        if (children.size() == 1) {
            auto root = children[0];
            return root;
        }

        std::vector<std::shared_ptr<Node>> current_level;

        for (size_t i = 0; i < children.size(); i += fanout_) {
            auto parent = std::make_shared<NonLeafNode>();

            size_t end = std::min(i + fanout_, children.size());
            for (size_t j = i; j < end; ++j) {
                parent->children.push_back(children[j]);
                parent->keys.push_back(children[j]->navigation_key());
            }
            parent->computeHash();
            parent->level_ = parent->children[0]->level_ + 1;
            current_level.push_back(parent);
        }
        return build_tree(current_level);
    }

    std::shared_ptr<Node> find_leaf(const std::shared_ptr<Node> &current_root, const KeyType &key) {
        std::shared_ptr<Node> current = current_root;

        while (current->type() == NON_LEAF) {
            auto inner = std::dynamic_pointer_cast<NonLeafNode>(current);
            auto it = std::lower_bound(inner->keys.begin(), inner->keys.end(), key);
            if (it == inner->keys.end()) {
                std::cout << "insert: non-leaf node out of range (not handled yet)" << std::endl;
                exit(-1);
            }
            current = inner->children[it - inner->keys.begin()];
        }
        return current;
    }

    std::shared_ptr<std::vector<std::shared_ptr<Node>>> find_path_base(const std::shared_ptr<Node> &current_root, const uint64_t &key) {
        std::shared_ptr<Node> current = current_root;
        auto path = std::make_shared<std::vector<std::shared_ptr<Node>>>();

        path->push_back(current);
        while (current->type() == NON_LEAF) {
            auto inner = std::dynamic_pointer_cast<NonLeafNode>(current);
            auto it = std::lower_bound(inner->keys.begin(), inner->keys.end(), key);
            if (it == inner->keys.end()) {
                std::cout << "insert: non-leaf node out of range (not handled yet)" << std::endl;
                throw "insert: non-leaf node out of range (not handled yet)";
            }
            current = inner->children[it - inner->keys.begin()];
            path->push_back(current);
        }
        return path;
    }

    std::shared_ptr<MerkleProof> prove_base(const std::shared_ptr<Node> &current_root, const uint64_t &key) {
        auto proof = std::make_shared<MerkleProof>();
        auto current = current_root;
        while (current->type() == NON_LEAF) {
            auto current_nl = std::dynamic_pointer_cast<NonLeafNode>(current);
            auto idx = std::lower_bound(current_nl->keys.begin(), current_nl->keys.end(), key) - current_nl->keys.begin();
            int i = 0;
            for (; i < idx; ++i) {
                proof->hashes.push_front({MerkleProof::LEFT, current_nl->children[i]->Hash()});
            }
            ++i;
            for (; i < current_nl->children.size() ; ++i) {
                proof->hashes.push_front({MerkleProof::RIGHT, current_nl->children[i]->Hash()});
            }
            current = current_nl->children[idx];
        }
        auto current_leaf = std::dynamic_pointer_cast<LeafBucketNode>(current);
        auto idx = std::lower_bound(current_leaf->keys.begin(), current_leaf->keys.end(), key) - current_leaf->keys.begin();
        int i = 0;
        for (; i < idx; ++i) {
            proof->hashes.push_front({MerkleProof::LEFT, current_leaf->datas[i].hash()});
        }
        ++i;
        for (; i < current_leaf->datas.size() ; ++i) {
            proof->hashes.push_front({MerkleProof::RIGHT, current_leaf->datas[i].hash()});
        }
        return proof;
    }

public:
    virtual void insert(const KeyType &key, const DataType &value, bool cal_hash)=0;
    virtual std::tuple<std::string, bool> Get(const KeyType &key)=0;
    virtual std::shared_ptr<std::vector<std::shared_ptr<Node>>> findPath(const std::shared_ptr<Node> &current_root, const uint64_t &key)=0;
    virtual std::shared_ptr<MerkleProof> prove(const std::shared_ptr<Node> &current_root, const uint64_t &key)=0;

    bool lazyPointUpdate(const uint64_t & key, bool force=false) {
        auto path = findPath(root_, key);
        if (path == nullptr) {
            return false;
        }

        {
            auto node = path->back();
            if (node->type() == NON_LEAF) {
                std::cout << "[MBT][lazyPointUpdate] bucket node not found" << std::endl;
                throw "[MBT][lazyPointUpdate] bucket node not found";
            }

            auto leaf = std::dynamic_pointer_cast<LeafBucketNode>(node);
#ifdef COW
            leaf->copy();
#endif
            auto it = std::lower_bound(leaf->keys.begin(), leaf->keys.end(), key);
            auto idx = it - leaf->keys.begin();
            if ((*it) == key) {
                leaf->datas[idx].update_hash();
            } else {
                std::cout << "[MBT][lazyPointUpdate] in bucket node, key not found" << std::endl;
                throw "[MBT][lazyPointUpdate] in bucket node, key not found";
            }
        }

        for (auto it = path->rbegin(); it != path->rend() ; ++it) {
            auto node = *it;
#ifdef COW
            node->copy();
#endif
            node->computeHash();
        }
        return true;
    }

    bool lazyBatchUpdate(const std::set<uint64_t> &dirty_set) {
        std::priority_queue<std::shared_ptr<Node>, std::vector<std::shared_ptr<Node>>, std::greater<>> q;
        std::set<std::shared_ptr<Node>> s;
        for (const auto &key: dirty_set) {
            auto path = findPath(root_, key);
            if (path == nullptr) {
                std::cout << "[MBT][lazyBatchUpdate] key path not found" << std::endl;
                throw "[MBT][lazyBatchUpdate] key path not found";
            }

            {
                auto node = path->back();
                if (node->type() == NON_LEAF) {
                    std::cout << "[MBT][lazyBatchUpdate] bucket node not found" << std::endl;
                    throw "[MBT][lazyBatchUpdate] bucket node not found";
                }

                auto leaf = std::dynamic_pointer_cast<LeafBucketNode>(node);
#ifdef COW
                leaf->copy();
#endif
                auto it = std::lower_bound(leaf->keys.begin(), leaf->keys.end(), key);
                auto idx = it - leaf->keys.begin();
                if ((*it) == key) {
                    leaf->datas[idx].update_hash();
                } else {
                    std::cout << "[MBT][lazyBatchUpdate] in bucket node, key not found" << std::endl;
                    throw "[MBT][lazyBatchUpdate] in bucket node, key not found";
                }
            }
            for (auto node: *path) {
                if (s.count(node) == 1)
                    continue;
                q.push(node);
                s.insert(node);
            }
        }

        while (!q.empty()) {
            auto node = q.top();
            q.pop();
#ifdef COW
            node->copy();
#endif
            node->computeHash();
        }
        return true;
    }
};

#endif
