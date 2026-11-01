#include <iostream>
#include <vector>
#include <memory>
#include <openssl/sha.h>

const int ORDER = 4;

class MerkleBPlusNode {
public:
    std::vector<int> keys;
    std::string hash;
    bool is_leaf;

    virtual ~MerkleBPlusNode() = default;
    virtual void update_hash() = 0;
};

class LeafNode : public MerkleBPlusNode {
public:
    std::vector<std::string> values;
    std::shared_ptr<LeafNode> next;

    LeafNode() { this->is_leaf = true; }

    void update_hash() override {
        unsigned char hash[SHA256_DIGEST_LENGTH];
        std::string data;
        for(auto& v : values) data += v;
        SHA256((unsigned char*)data.c_str(), data.size(), hash);
        this->hash = std::string((char*)hash, SHA256_DIGEST_LENGTH);
    }
};

class InternalNode : public MerkleBPlusNode {
public:
    std::vector<std::shared_ptr<MerkleBPlusNode>> children;

    InternalNode() { this->is_leaf = false; }

    void update_hash() override {
        std::string combined;
        for(auto& child : children) {
            combined += child->hash;
        }
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256((unsigned char*)combined.c_str(), combined.size(), hash);
        this->hash = std::string((char*)hash, SHA256_DIGEST_LENGTH);
    }
};

class MerkleBPlusTree {
private:
    std::shared_ptr<MerkleBPlusNode> root;

    void insert_non_full(std::shared_ptr<MerkleBPlusNode> node, int key,
                       std::string value) {
        if (auto leaf = std::dynamic_pointer_cast<LeafNode>(node)) {

            auto pos = lower_bound(leaf->keys.begin(), leaf->keys.end(), key);
            int idx = pos - leaf->keys.begin();
            leaf->keys.insert(pos, key);
            leaf->values.insert(leaf->values.begin()+idx, value);
            leaf->update_hash();
        } else {
            auto internal = std::dynamic_pointer_cast<InternalNode>(node);

            int i = 0;
            while (i < internal->keys.size() && key >= internal->keys[i]) i++;

            if (internal->children[i]->keys.size() == ORDER-1) {
                split_child(internal, i);
                if (key > internal->keys[i]) i++;
            }
            insert_non_full(internal->children[i], key, value);
            internal->update_hash();
        }
    }

    void split_child(std::shared_ptr<InternalNode> parent, int idx) {
        auto child = parent->children[idx];
        auto new_node = std::make_shared<InternalNode>();

        if (child->is_leaf) new_node = std::make_shared<LeafNode>();

        int mid = (ORDER-1)/2;
        new_node->keys.assign(child->keys.begin()+mid+1, child->keys.end());
        child->keys.resize(mid);

        if (!child->is_leaf) {
            auto internal_child = std::dynamic_pointer_cast<InternalNode>(child);
            new_node->children.assign(internal_child->children.begin()+mid+1,
                                     internal_child->children.end());
            internal_child->children.resize(mid+1);
        }

        parent->keys.insert(parent->keys.begin()+idx, child->keys[mid]);
        parent->children.insert(parent->children.begin()+idx+1, new_node);

        child->update_hash();
        new_node->update_hash();
        parent->update_hash();
    }

public:
    MerkleBPlusTree() : root(std::make_shared<LeafNode>()) {}

    void insert(int key, std::string value) {
        if (root->keys.size() == ORDER-1) {
            auto new_root = std::make_shared<InternalNode>();
            new_root->children.push_back(root);
            split_child(new_root, 0);
            root = new_root;
        }
        insert_non_full(root, key, value);
        root->update_hash();
    }

    std::string get_root_hash() {
        return root->hash;
    }

    std::vector<std::string> generate_proof(int key) {
        std::vector<std::string> proof;
        auto node = root;

        while (!node->is_leaf) {
            auto internal = std::dynamic_pointer_cast<InternalNode>(node);
            int i = 0;
            while (i < internal->keys.size() && key >= internal->keys[i]) i++;

            if(i > 0) proof.push_back(internal->children[i-1]->hash);
            if(i < internal->children.size()-1)
                proof.push_back(internal->children[i+1]->hash);

            node = internal->children[i];
        }
        return proof;
    }
};

#include <algorithm>

class MerkleBPlusTree {

public:

    MerkleBPlusTree(const std::vector<std::pair<int, std::string>>& data) {

        auto sorted_data = data;
        std::sort(sorted_data.begin(), sorted_data.end(),
            [](auto& a, auto& b) { return a.first < b.first; });

        for (auto& [key, value] : sorted_data) {
            insert(key, value);
        }
    }

    void remove(int key) {
        if (!root) return;

        remove_recursive(root, key);

        if (!root->is_leaf && root->keys.empty()) {
            root = std::dynamic_pointer_cast<InternalNode>(root)->children[0];
        }
        update_path_hashes(root.get());
    }

private:

    void remove_recursive(std::shared_ptr<MerkleBPlusNode> node, int key) {
        if (auto leaf = std::dynamic_pointer_cast<LeafNode>(node)) {

            auto pos = std::lower_bound(leaf->keys.begin(), leaf->keys.end(), key);
            if (pos != leaf->keys.end() && *pos == key) {
                int idx = pos - leaf->keys.begin();
                leaf->keys.erase(pos);
                leaf->values.erase(leaf->values.begin() + idx);
                leaf->update_hash();

                if (leaf->keys.size() < (ORDER-1)/2) {
                    handle_leaf_underflow(leaf);
                }
            }
        } else {
            auto internal = std::dynamic_pointer_cast<InternalNode>(node);
            int i = 0;
            while (i < internal->keys.size() && key >= internal->keys[i]) ++i;

            remove_recursive(internal->children[i], key);

            if (internal->children[i]->keys.size() < (ORDER-1)/2) {
                handle_internal_underflow(internal, i);
            }
            internal->update_hash();
        }
    }

    void handle_leaf_underflow(std::shared_ptr<LeafNode> leaf) {
        auto parent = std::dynamic_pointer_cast<InternalNode>(leaf->parent_);
        int idx = -1;
        for (size_t i = 0; i < parent->children.size(); ++i) {
            if (parent->children[i].get() == leaf.get()) {
                idx = i;
                break;
            }
        }

        if (idx > 0) {
            auto left_sib = std::dynamic_pointer_cast<LeafNode>(parent->children[idx-1]);
            if (left_sib->keys.size() > (ORDER-1)/2) {

                leaf->keys.insert(leaf->keys.begin(), left_sib->keys.back());
                leaf->values.insert(leaf->values.begin(), left_sib->values.back());
                left_sib->keys.pop_back();
                left_sib->values.pop_back();

                parent->keys[idx-1] = leaf->keys.front();
                left_sib->update_hash();
                leaf->update_hash();
                return;
            }
        }

        if (idx < parent->children.size()-1) {
            auto right_sib = std::dynamic_pointer_cast<LeafNode>(parent->children[idx+1]);
            if (right_sib->keys.size() > (ORDER-1)/2) {

                leaf->keys.push_back(right_sib->keys.front());
                leaf->values.push_back(right_sib->values.front());
                right_sib->keys.erase(right_sib->keys.begin());
                right_sib->values.erase(right_sib->values.begin());

                parent->keys[idx] = right_sib->keys.front();
                leaf->update_hash();
                right_sib->update_hash();
                return;
            }
        }

        if (idx > 0) {
            merge_leaf_nodes(parent, idx-1, idx);
        } else {
            merge_leaf_nodes(parent, idx, idx+1);
        }
    }

    void merge_leaf_nodes(std::shared_ptr<InternalNode> parent, int left_idx, int right_idx) {
        auto left = std::dynamic_pointer_cast<LeafNode>(parent->children[left_idx]);
        auto right = std::dynamic_pointer_cast<LeafNode>(parent->children[right_idx]);

        left->keys.insert(left->keys.end(), right->keys.begin(), right->keys.end());
        left->values.insert(left->values.end(), right->values.begin(), right->values.end());
        left->next = right->next;

        parent->keys.erase(parent->keys.begin() + left_idx);
        parent->children.erase(parent->children.begin() + right_idx);

        left->update_hash();
        parent->update_hash();
    }

    void handle_internal_underflow(std::shared_ptr<InternalNode> node, int idx) {

    }

    void update_path_hashes(MerkleBPlusNode* node) {
        while (node) {
            node->update_hash();
            node = node->parent_;
        }
    }
};

int main() {

    std::vector<std::pair<int, std::string>> data = {
        {5, "Data5"}, {10, "Data10"}, {20, "Data20"},
        {15, "Data15"}, {25, "Data25"}
    };
    MerkleBPlusTree tree(data);

    std::cout << "Initial root hash: ";
    print_hash(tree.get_root_hash());

    tree.remove(10);
    std::cout << "After deletion root hash: ";
    print_hash(tree.get_root_hash());

    auto proof = tree.generate_proof(10);
    std::cout << "Proof of deleted key exists: " << !proof.empty() << std::endl;

    return 0;
}

class MerkleBPlusTree {
public:

    MerkleBPlusTree(const std::vector<std::pair<int, std::string>>& data) {
        if (data.empty()) {
            root = nullptr;
            return;
        }

        auto sorted_data = data;
        std::sort(sorted_data.begin(), sorted_data.end());
        std::vector<std::shared_ptr<LeafNode>> leaves = create_leaves(sorted_data);

        root = build_tree(leaves);
        root->update_hash();
    }

private:

    std::vector<std::shared_ptr<LeafNode>>
    create_leaves(const std::vector<std::pair<int, std::string>>& data) {
        std::vector<std::shared_ptr<LeafNode>> leaves;
        std::shared_ptr<LeafNode> prev;

        for (size_t i = 0; i < data.size(); i += ORDER-1) {
            auto leaf = std::make_shared<LeafNode>();
            size_t end = std::min(i + ORDER-1, data.size());

            for (size_t j = i; j < end; ++j) {
                leaf->keys.push_back(data[j].first);
                leaf->values.push_back(data[j].second);
            }

            if (prev) prev->next = leaf;
            prev = leaf;
            leaf->update_hash();
            leaves.push_back(leaf);
        }
        return leaves;
    }

    std::shared_ptr<MerkleBPlusNode>
    build_tree(const std::vector<std::shared_ptr<MerkleBPlusNode>>& children) {
        if (children.size() == 1) return children[0];

        std::vector<std::shared_ptr<MerkleBPlusNode>> current_level;

        for (size_t i = 0; i < children.size(); i += ORDER) {
            auto parent = std::make_shared<InternalNode>();
            size_t end = std::min(i + ORDER, children.size());

            for (size_t j = i; j < end; ++j) {
                parent->children.push_back(children[j]);
                children[j]->parent_ = parent.get();
            }

            for (size_t j = 0; j < parent->children.size()-1; ++j) {
                auto& ckeys = parent->children[j]->keys;
                parent->keys.push_back(ckeys.back());
            }

            parent->update_hash();
            current_level.push_back(parent);
        }
        return build_tree(current_level);
    }

};