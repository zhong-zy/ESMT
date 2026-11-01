
#ifndef SHRUBS_LIKE_INDEX_MPT_H
#define SHRUBS_LIKE_INDEX_MPT_H

#include <iostream>
#include <utility>
#include <vector>
#include <memory>
#include <sstream>
#include <stack>
#include <list>
#include <algorithm>
#include "cpputils/sha256ssl.h"
typedef uint64_t KeyType;
#include "dbmaintainer/indices/DataStore/MemStore.h"
#include "dbmaintainer/mpt/encoding.h"
using namespace mpt;

static size_t nibbles_it_len(std::tuple<std::vector<uint8_t>::iterator, std::vector<uint8_t>::iterator> nibbles_it) {
    return std::get<1>(nibbles_it) - std::get<0>(nibbles_it);
}

typedef MemData DataItem;
class MerklePatriciaTrie {
private:
    enum NodeType {
        BRANCH,
        LEAF,
        EXTENSION,
    };
    class Node {
    public:
        virtual ~Node() = default;
        int level_ = -1;

        virtual std::string Hash()=0;
        virtual NodeType type()=0;
        virtual void computeHash()=0;
        virtual bool &Dirty()=0;
        virtual void copy()=0;

        friend bool operator<(std::shared_ptr<Node> &a, std::shared_ptr<Node> &b) {
            return a->level_ < b->level_;
        }
        friend bool operator>(std::shared_ptr<Node> &a, std::shared_ptr<Node> &b) {
            return a->level_ > b->level_;
        }
    };

    class LeafExtensionNode: public Node {
    public:
        std::vector<uint8_t> path_;
        std::shared_ptr<Node> next_node_;
        DataItem data_;
        std::string hash_;
        bool is_leaf_;
        bool dirty_=true;

        void copy() {
            auto new_node = std::make_shared<LeafExtensionNode>();
            new_node->path_ = path_;
            new_node->next_node_ = next_node_;
            new_node->data_ = data_;
            new_node->hash_ = hash_;
            new_node->is_leaf_ = is_leaf_;
            new_node->dirty_ = dirty_;
        }
        NodeType type() {
            if (is_leaf_) {
                return LEAF;
            } else {
                return EXTENSION;
            }
        }
        std::string Hash() {
            return hash_;
        }
        bool &Dirty() {
            return dirty_;
        }

        void computeHash() {
            hash_ = cpputils::HashFunction(hash_data_lazy());
            dirty_ = false;
        }

        std::string hash_data_lazy() {
            std::stringstream row_data;

            row_data << HexToBytesString(path_);
            row_data << "|";
            if (is_leaf_) {
                row_data << data_.data();
            } else {
                row_data << next_node_->Hash();
            }

            return row_data.str();
        }

        LeafExtensionNode(std::tuple<std::vector<uint8_t>::iterator, std::vector<uint8_t>::iterator> &nibbles_it, std::string &value):
                path_(std::get<0>(nibbles_it), std::get<1>(nibbles_it)),
                data_(0, value),
                is_leaf_(true){}

        LeafExtensionNode(std::tuple<std::vector<uint8_t>::iterator, std::vector<uint8_t>::iterator> &nibbles_it, DataItem &data):
                path_(std::get<0>(nibbles_it), std::get<1>(nibbles_it)),
                data_(data),
                is_leaf_(true){}

        LeafExtensionNode(std::tuple<std::vector<uint8_t>::iterator, std::vector<uint8_t>::iterator> &nibbles_it, std::shared_ptr<Node> node):
                path_(std::get<0>(nibbles_it), std::get<1>(nibbles_it)),
                next_node_(std::move(node)),
                is_leaf_(false){}
        LeafExtensionNode(){};
    };

    class BranchNode: public Node {
    public:
        std::shared_ptr<Node> children_[16];
        std::string hash_;
        DataItem data_;
        bool dirty_=true;
        bool has_value_=false;

        void copy() {
            auto new_node = std::make_shared<BranchNode>();

            for (int i = 0; i < 16; ++i) {
                new_node->children_[i] = children_[i];
            }
            new_node->hash_ = hash_;
            new_node->data_ = data_;
            new_node->dirty_ = dirty_;
            new_node->has_value_ = has_value_;
        }
        NodeType type() {
            return BRANCH;
        }
        std::string Hash() {
            return hash_;
        }
        bool &Dirty() {
            return dirty_;
        }

        void computeHash() {
            hash_ = cpputils::HashFunction(hash_data_lazy());
            dirty_ = false;
        }

        bool has_value() {
            return has_value_;
        }

        std::string hash_data_lazy() {
            std::stringstream row_data;
            for (auto & i : children_) {
                if (i == nullptr) {
#ifdef HASH_NULL
                    row_data << cpputils::HashFunction("null") << "|";
#else
                    row_data << "null" << "|";
#endif
                } else {
                    row_data << i->Hash() << "|";
                }
            }
            row_data << data_.data();
            return row_data.str();
        }

        void set_data(DataItem &data) {
            data_ = data;
            dirty_ = true;
            has_value_ = true;
        }

        void set_value(std::string &value) {
            data_.update(value);
            dirty_ = true;
            has_value_ = true;
        }

        void set_branch(uint8_t branch_nibble, std::shared_ptr<Node> new_leaf) {
            children_[branch_nibble] = std::move(new_leaf);
            dirty_ = true;
        }
    };

    std::shared_ptr<Node> root_ = nullptr;

public:
    MerklePatriciaTrie() = default;

    void build(std::vector<KeyType>::iterator st, std::vector<KeyType>::iterator ed) {
        std::sort(st, ed);
        for (auto it = st; it != ed; ++it) {
            auto key = std::to_string(*it);
            insert(key, key);
        }
        init_hash();
    }

    std::string getRootHash() {
        return root_->Hash();
    }

    std::tuple<std::string, bool> Get(std::string &key) {
        auto node = &root_;
        auto nibbles = KeyBytesToHex(key);
        while (true) {
            if ((*node) == nullptr) {
                return {"", false};
            }

            if ((*node)->type() == LEAF) {
                auto leaf = std::dynamic_pointer_cast<LeafExtensionNode>((*node));
                auto matched = PrefixLen(leaf->path_, nibbles);
                if (matched != leaf->path_.size() || matched != nibbles.size()) {
                    return {"", false};
                }
                return {leaf->data_.data(), true};
            }

            if ((*node)->type() == BRANCH) {
                auto branch = std::dynamic_pointer_cast<BranchNode>((*node));
                if (nibbles.empty()) {
                    return {branch->data_.data(), branch->has_value()};
                }

                auto b = nibbles[0];
                nibbles = {nibbles.begin()+1, nibbles.end()};
                node = &branch->children_[b];
                continue;
            }

            if ((*node)->type() == EXTENSION) {
                auto ext = std::dynamic_pointer_cast<LeafExtensionNode>((*node));
                auto matched = PrefixLen(ext->path_, nibbles);

                if (matched < ext->path_.size()) {
                    return {"", false};
                }

                nibbles = {nibbles.begin()+matched, nibbles.end()};
                node = &ext->next_node_;
                continue;
            }
            printf("not found");
            return {"", false};
        }
    }

    void insert(std::string &key, std::string &value, bool cal_hash=true) {

        auto node = &root_;
        auto nibbles = KeyBytesToHex(key);
        while (true) {

            if ((*node) == nullptr) {
                auto nibbles_it = std::make_tuple(nibbles.begin(), nibbles.end());
                auto leaf = std::make_shared<LeafExtensionNode>(nibbles_it, value);
                (*node) = leaf;
                return;
            }

            if ((*node)->type() == LEAF) {
                auto leaf = std::dynamic_pointer_cast<LeafExtensionNode>((*node));
                auto matched = PrefixLen(leaf->path_, nibbles);

                if (matched == nibbles.size() && matched == leaf->path_.size()) {

                    leaf->data_.update(value);

                    return;
                }

                auto branch = std::make_shared<BranchNode>();
                if (matched == leaf->path_.size()) {
                    branch->set_data(leaf->data_);
                }

                if (matched == nibbles.size()) {
                    branch->set_value(value);
                }

                if (matched > 0) {

                    auto leaf_path_it = std::make_tuple(leaf->path_.begin(), leaf->path_.begin()+matched);
                    auto ext = std::make_shared<LeafExtensionNode>(leaf_path_it, branch);
                    (*node) = ext;
                } else {

                    (*node) = branch;
                }

                if (matched < leaf->path_.size()) {

                    auto branch_nibble  = leaf->path_[matched];

                    auto leaf_nibbles_it = std::make_tuple(leaf->path_.begin()+matched+1, leaf->path_.end());
                    auto new_leaf = std::make_shared<LeafExtensionNode>(leaf_nibbles_it, leaf->data_);
                    branch->set_branch(branch_nibble, new_leaf);
                }

                if (matched < nibbles.size()) {

                    auto branch_nibble = nibbles[matched];

                    auto leaf_nibbles_it = std::make_tuple(nibbles.begin()+matched+1, nibbles.end());
                    auto new_leaf = std::make_shared<LeafExtensionNode>(leaf_nibbles_it, value);
                    branch->set_branch(branch_nibble, new_leaf);
                }
                return;
            }

            if ((*node)->type() == BRANCH) {
                auto branch = std::dynamic_pointer_cast<BranchNode>((*node));
                if (nibbles.empty()) {

                    branch->set_value(value);
                    return;
                }

                auto branch_nibble = nibbles[0];
                nibbles = {nibbles.begin()+1, nibbles.end()};
                node = &branch->children_[branch_nibble];
                continue;
            }

            if ((*node)->type() == EXTENSION) {
                auto ext = std::dynamic_pointer_cast<LeafExtensionNode>((*node));
                auto matched = PrefixLen(ext->path_, nibbles);
                if (matched < ext->path_.size()) {

                    auto ext_nibbles = std::make_tuple(ext->path_.begin(), ext->path_.begin()+matched);
                    auto branch_nibble = ext->path_[matched];
                    auto ext_remaining_nibbles = std::make_tuple(ext->path_.begin()+matched+1, ext->path_.end());
                    auto branch = std::make_shared<BranchNode>();
                    if (nibbles_it_len(ext_remaining_nibbles) == 0) {

                        branch->set_branch(branch_nibble, ext->next_node_);
                    } else {

                        auto new_ext = std::make_shared<LeafExtensionNode>(ext_remaining_nibbles, ext->next_node_);
                        branch->set_branch(branch_nibble, new_ext);
                    }

                    if (matched < nibbles.size()) {
                        auto node_branch_nibble = nibbles[matched];
                        auto node_leaf_nibbles = std::make_tuple(nibbles.begin()+matched+1, nibbles.end());
                        auto remaining_leaf = std::make_shared<LeafExtensionNode>(node_leaf_nibbles, value);
                        branch->set_branch(node_branch_nibble, remaining_leaf);
                    } else if (matched == nibbles.size()) {
                        branch->set_value(value);
                    } else {
                        printf("too many matched (%d > %ld)\n", matched, nibbles.size());
                        exit(-1);
                    }

                    if (nibbles_it_len(ext_nibbles) == 0) {
                        (*node) = branch;
                    } else {
                        (*node) = std::make_shared<LeafExtensionNode>(ext_nibbles, branch);
                    }
                    return;
                }

                nibbles = {nibbles.begin()+matched, nibbles.end()};
                node = &ext->next_node_;
                continue;
            }
            printf("unknown type");
            exit(-1);
        }
    }

    std::shared_ptr<std::vector<std::shared_ptr<Node>>> findPath(const std::string &key) {
        auto path = std::make_shared<std::vector<std::shared_ptr<Node>>>();

        auto node = root_;
        auto nibbles = KeyBytesToHex(key);
        path->push_back(node);
        while (true) {
            if (node == nullptr) {
                return {nullptr};
            }

            if (node->type() == LEAF) {
                auto leaf = std::dynamic_pointer_cast<LeafExtensionNode>(node);
                auto matched = PrefixLen(leaf->path_, nibbles);
                if (matched != leaf->path_.size() || matched != nibbles.size()) {
                    return {nullptr};
                }
                path->push_back(node);
                break;
            }

            if (node->type() == BRANCH) {
                auto branch = std::dynamic_pointer_cast<BranchNode>(node);
                if (nibbles.empty()) {
                    path->push_back(node);
                    break;
                }

                auto b = nibbles[0];
                nibbles = {nibbles.begin()+1, nibbles.end()};
                node = branch->children_[b];
                path->push_back(node);
                continue;
            }

            if (node->type() == EXTENSION) {
                auto ext = std::dynamic_pointer_cast<LeafExtensionNode>(node);
                auto matched = PrefixLen(ext->path_, nibbles);

                if (matched < ext->path_.size()) {
                    return {nullptr};
                }

                nibbles = {nibbles.begin()+matched, nibbles.end()};
                node = ext->next_node_;
                path->push_back(node);
                continue;
            }
            printf("not found");
            return {nullptr};
        }

        return path;
    }

    void init_hash() {

        std::stack<std::shared_ptr<Node>> st1, st2;
        st1.push(root_);
        while (!st1.empty()) {
            auto node = st1.top();
            st1.pop();
            st2.push(node);
            if (node->type() == BRANCH) {
                auto branch = std::dynamic_pointer_cast<BranchNode>(node);
                for (const auto &child: branch->children_) {
                    if (child != nullptr) {
                        st1.push(child);
                    }
                }
            } else if (node->type() == EXTENSION) {
                auto ext = std::dynamic_pointer_cast<LeafExtensionNode>(node);
                st1.push(ext->next_node_);
            }
        }
        std::cout << "[init hash] total nodes: " << st2.size() << std::endl;
        while (!st2.empty()) {
            auto node = st2.top();
            st2.pop();

            node->computeHash();
        }
    }

    void rec_hash(std::shared_ptr<Node> node) {

        if (node->type() == BRANCH) {
            auto branch = std::dynamic_pointer_cast<BranchNode>(node);
            for (const auto &child: branch->children_) {
                if (child != nullptr) {
                    rec_hash(child);
                }
            }
        } else if (node->type() == EXTENSION) {
            auto ext = std::dynamic_pointer_cast<LeafExtensionNode>(node);
            rec_hash(ext->next_node_);
        }
        node->computeHash();
    }

    bool lazyPointUpdate(const std::string& key, bool force=false) {
        auto path = findPath(key);
        if (path == nullptr) {
            return false;
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

    bool lazyBatchUpdate(const std::set<std::string> &dirty_set) {
        std::priority_queue<std::shared_ptr<Node>, std::vector<std::shared_ptr<Node>>, std::less<>> q;
        std::set<std::shared_ptr<Node>> s;

        for (const auto &key: dirty_set) {
            auto path = findPath(key);
            if (path == nullptr) {
                std::cout << "[MPT][lazyBatchUpdate] key path not found" << std::endl;
                throw "[MPT][lazyBatchUpdate] key path not found";
            }
            int level = 0;
            for (auto node: *path) {
                if (s.count(node) == 1)
                    continue;
                node->level_ = level;
                ++level;
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

    std::shared_ptr<MerkleProof> prove(const std::string &key) {
        auto proof = std::make_shared<MerkleProof>();

        auto node = root_;
        auto nibbles = KeyBytesToHex(key);
        while (true) {
            if (node == nullptr) {
                return {nullptr};
            }

            if (node->type() == LEAF) {
                auto leaf = std::dynamic_pointer_cast<LeafExtensionNode>(node);
                auto matched = PrefixLen(leaf->path_, nibbles);
                if (matched != leaf->path_.size() || matched != nibbles.size()) {
                    return {nullptr};
                }

                break;
            }

            if (node->type() == BRANCH) {
                auto branch = std::dynamic_pointer_cast<BranchNode>(node);
                if (nibbles.empty()) {

                    for (int i = 0; i < 16; ++i) {
                        if (branch->children_[i] == nullptr) {
#ifdef HASH_NULL
                            proof->hashes.push_front({MerkleProof::LEFT, cpputils::HashFunction("null")});
#else
                            proof->hashes.push_front({MerkleProof::LEFT, "null"});
#endif
                        } else {
                            proof->hashes.push_front({MerkleProof::LEFT, branch->children_[i]->Hash()});
                        }
                    }
                    break;
                }

                auto b = nibbles[0];
                nibbles = {nibbles.begin()+1, nibbles.end()};
                node = branch->children_[b];

                int i = 0;
                for (; i < b; ++i) {
                    if (branch->children_[i] == nullptr) {
#ifdef HASH_NULL
                        proof->hashes.push_front({MerkleProof::LEFT, cpputils::HashFunction("null")});
#else
                        proof->hashes.push_front({MerkleProof::LEFT, "null"});
#endif
                    } else {
                        proof->hashes.push_front({MerkleProof::LEFT, branch->children_[i]->Hash()});
                    }
                }
                ++i;
                for (; i < 16; ++i) {
                    if (branch->children_[i] == nullptr) {
#ifdef HASH_NULL
                        proof->hashes.push_front({MerkleProof::RIGHT, cpputils::HashFunction("null")});
#else
                        proof->hashes.push_front({MerkleProof::RIGHT, "null"});
#endif
                    } else {
                        proof->hashes.push_front({MerkleProof::RIGHT, branch->children_[i]->Hash()});
                    }
                }
                continue;
            }

            if (node->type() == EXTENSION) {
                auto ext = std::dynamic_pointer_cast<LeafExtensionNode>(node);
                auto matched = PrefixLen(ext->path_, nibbles);

                if (matched < ext->path_.size()) {
                    return {nullptr};
                }

                nibbles = {nibbles.begin()+matched, nibbles.end()};
                node = ext->next_node_;

                continue;
            }
            printf("not found");
            return {nullptr};
        }
        return proof;
    }
};

#endif
