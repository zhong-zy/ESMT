#include <iostream>
#include <vector>
#include <string>
#include <openssl/sha.h>

std::string sha256(const std::string& str) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, str.c_str(), str.size());
    SHA256_Final(hash, &sha256);

    std::stringstream ss;
    for(int i = 0; i < SHA256_DIGEST_LENGTH; i++)
        ss << std::hex << (int)hash[i];
    return ss.str();
}

template<typename KeyType>
class MerkleBPlusTree {
private:

    struct Node {
        bool is_leaf;
        Node* parent;
        std::vector<KeyType> keys;

        virtual ~Node() = default;
        virtual std::string compute_hash() = 0;
    };

    struct NonLeafNode : public Node {
        std::vector<std::string> child_hashes;
        std::vector<int8_t> states;
        std::vector<Node*> children;

        NonLeafNode() { this->is_leaf = false; }

        std::string compute_hash() override {
            std::string concat;
            for(size_t i=0; i<children.size(); ++i) {
                concat += std::to_string(keys[i]) + child_hashes[i];
            }
            return sha256(concat);
        }
    };

    struct LeafNode : public Node {
        std::vector<std::string> data_hashes;
        std::vector<int8_t> states;
        std::vector<std::string> data;
        LeafNode* next;

        LeafNode() {
            this->is_leaf = true;
            next = nullptr;
        }

        std::string compute_hash() override {
            std::string concat;
            for(const auto& d : data) {
                concat += d;
            }
            return sha256(concat);
        }
    };

    Node* root;
    const int m;

public:
    MerkleBPlusTree(int order=2) : m(order), root(nullptr) {}

    void insert(const KeyType& key, const std::string& data, bool cal_hash=true) {
        if(!root) {
            root = new LeafNode();

        }

        auto leaf = find_leaf(key);
        insert_into_leaf(static_cast<LeafNode*>(leaf), key, data);

        if(cal_hash) {
            update_hash(key);
        }
    }

    void update_hash(const KeyType& key) {
        auto leaf = find_leaf(key);
        update_hash_from(leaf);
    }

private:

    void update_hash_from(Node* node) {
        while(node) {
            std::string new_hash = node->compute_hash();
            if(auto nl = dynamic_cast<NonLeafNode*>(node)) {
                size_t idx = get_parent_index(node);
                if(idx != -1) {
                    nl->parent->child_hashes[idx] = new_hash;
                    nl->parent->states[idx] = 0;
                }
            }
            node = node->parent;
        }
    }

    void split_child(NonLeafNode* parent, int child_idx) {

    }

    Node* find_leaf(const KeyType& key) const {  }
    size_t get_parent_index(Node* child) const {  }
};