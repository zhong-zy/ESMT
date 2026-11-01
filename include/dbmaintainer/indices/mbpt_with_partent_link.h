#include <vector>
#include <string>
#include <algorithm>
#include <functional>

std::string computeHash(const std::string& data) {
    std::hash<std::string> hasher;
    return std::to_string(hasher(data));
}

template <typename K, int M = 3>
class MerkleBPlusTree {
private:
    struct Node {
        bool is_leaf;
        bool is_root;
        Node* parent;
        std::vector<K> keys;
        std::vector<Node*> children;
        Node* next_leaf;
        std::string hash;

        std::vector<std::string> values;

        Node(bool leaf = false) : is_leaf(leaf), is_root(false), parent(nullptr), next_leaf(nullptr) {}
    };

    Node* root_;
    int max_degree_ = M;

    void updateHash(Node* node) {
        if (!node) return;

        std::string hash_data;
        if (node->is_leaf) {

            for (const auto& val : node->values) {
                hash_data += computeHash(val);
            }
        } else {

            for (auto child : node->children) {
                hash_data += child->hash;
            }
        }
        node->hash = computeHash(hash_data);

        if (node->parent) updateHash(node->parent);
    }

    void splitNode(Node* old_node) {
        Node* new_node = new Node(old_node->is_leaf);
        int split_pos = (max_degree_ + 1) / 2;

        auto mid_key = old_node->keys.begin() + split_pos;
        new_node->keys.assign(mid_key, old_node->keys.end());
        old_node->keys.erase(mid_key, old_node->keys.end());

        if (old_node->is_leaf) {
            new_node->values.assign(old_node->values.begin() + split_pos, old_node->values.end());
            old_node->values.erase(old_node->values.begin() + split_pos, old_node->values.end());
            new_node->next_leaf = old_node->next_leaf;
            old_node->next_leaf = new_node;
        } else {

            new_node->children.assign(old_node->children.begin() + split_pos, old_node->children.end());
            old_node->children.erase(old_node->children.begin() + split_pos, old_node->children.end());
            for (auto child : new_node->children) {
                child->parent = new_node;
            }
        }

        if (old_node->is_root) {
            Node* new_root = new Node();
            new_root->is_root = true;
            new_root->keys.push_back(new_node->keys.front());
            new_root->children.push_back(old_node);
            new_root->children.push_back(new_node);
            old_node->parent = new_root;
            new_node->parent = new_root;
            root_ = new_root;
        } else {
            auto parent = old_node->parent;
            auto insert_pos = std::upper_bound(parent->keys.begin(), parent->keys.end(), new_node->keys.front());
            parent->keys.insert(insert_pos, new_node->keys.front());
            parent->children.insert(parent->children.begin() + (insert_pos - parent->keys.begin()) + 1, new_node);
            new_node->parent = parent;
        }

        updateHash(old_node);
        updateHash(new_node);
    }

public:
    MerkleBPlusTree() : root_(new Node(true)) {
        root_->is_root = true;
    }

    void insert(const K& key, const std::string& value) {
        Node* current = root_;

        while (!current->is_leaf) {
            auto it = std::upper_bound(current->keys.begin(), current->keys.end(), key);
            current = current->children[it - current->keys.begin()];
        }

        auto insert_pos = std::upper_bound(current->keys.begin(), current->keys.end(), key);
        current->keys.insert(insert_pos, key);
        current->values.insert(current->values.begin() + (insert_pos - current->keys.begin()), value);

        if (current->keys.size() >= max_degree_) {
            splitNode(current);
        }

        updateHash(current);
    }

    std::string getRootHash() const {
        return root_->hash;
    }

    bool verify(const K& key, const std::string& value) {

        return true;
    }
};