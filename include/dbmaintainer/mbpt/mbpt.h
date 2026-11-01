#include <iostream>
#include <vector>
#include <algorithm>
#include <fstream>
#include <string>
#include <queue>
#include <set>
#include "merkle.h"

using namespace merkle;

#ifndef MERKLE_B_PLUS_TREE_H
#define MERKLE_B_PLUS_TREE_H

#define MBPT_DISK

#include "cpputils/SHA256.h"
#include "dbmaintainer/indices/ADSStore/ADSStore.h"
#include "dbmaintainer/indices/DataStore/stream.h"
#include "cpputils/sha256ssl.h"
using namespace indices;

typedef uint64_t KeyType;

class Node {
public:
    bool isLeaf;
    bool isDirty;
    int layer;
    std::string hash;
    Node *father;
    std::vector<KeyType> keys;

    Node *ptr2next;
    union ptr {
        std::vector<Node *> ptr2Tree;
        std::vector<std::string> dataPtr;

        ptr();
        ~ptr();
    } ptr2TreeOrData;

    Node();

    void print() {
        std::cout << "Node info ( key = ";
        for (const auto &item: keys) std::cout << item << " ";
        std::cout << ")\n";
        std::cout << "\tlayer: " << layer << std::endl <<
             "\tisLeaf: " << isLeaf << std::endl <<
             "\tisDirty: " << isDirty << std::endl <<
             "\thash: " << hash << std::endl;
        if (isLeaf) {
            std::cout << "\tdataPtr: ";
            for (const auto &item: ptr2TreeOrData.dataPtr) std::cout << item << " ";
            std::cout << "\n";
        } else {
            std::cout << "\ttreePtr: ";
            for (const auto &item: ptr2TreeOrData.ptr2Tree) std::cout << item->keys.size() << " ";
            std::cout << "\n";
        }
    }

    friend class MerkleBPlusTree;
};

struct cmp {
    bool operator()(Node *a, Node *b) {
        return (a)->layer > (b)->layer;
    }
};

class MerkleBPlusTree {

public:
    int maxIntChildLimit;
    int maxLeafNodeLimit;
    unsigned long long statistic_point;
    unsigned long long statistic_miss;
    unsigned long long statistic_batch;
    Node *root;

    std::shared_ptr<ADSStore> db2_, data_db_;

    int tree_id_ = 0;

    void insertInternal(KeyType x, Node **cursor, Node **child);

    Node **findParent(Node *cursor, Node *child);

    Node *firstLeftNode(Node *cursor);

    std::string getKey(Node *cursor) {
        if (cursor == nullptr)
            return "";
        return std::to_string(cursor->layer) + "|" + (cursor->keys.empty() ? "null" : std::to_string(cursor->keys[0]));
    }

    std::string getValue(Node *cursor) {
        if (cursor == nullptr)
            return "";
        std::string builder = "key";

        for (const auto &item: cursor->keys) builder += (std::to_string(item) + "|");
        builder += (std::to_string(cursor->layer) + "|");
        builder += (std::to_string(cursor->isLeaf) + "|");
        builder += (std::to_string(cursor->isDirty) + "|");
        builder += (cursor->hash + "|");
        return builder;
    }

    void split(const std::string &s, std::vector<std::string> &node) {
        std::string tmp;
        for (char i: s) {
            if (i == '|') {
                node.push_back(tmp);
                tmp.clear();
                continue;
            }
            tmp.push_back(i);
        }
    }

    void read(Node *cursor) {

        auto val = db2_->Get(tree_id_, getKey(cursor));

    }

    std::string computeHash(Node *cursor, bool check = false) {
#ifndef SSL_SHA256
        cpputils::SHA256 sha;
#endif
        if (check) {
            auto key = getKey(cursor);

#ifndef SSL_SHA256
            sha.update(getValue(cursor));
            auto value = cpputils::SHA256::toString(sha.digest());

#else

            auto value = cpputils::HashFunction(getValue(cursor));

#endif

            db2_->Put(tree_id_, key, value);
        }
        if (cursor->isLeaf) {
            cursor->layer = 0;

#ifndef SSL_SHA256
            sha.update(std::to_string(cursor->keys[0]) + cursor->ptr2TreeOrData.dataPtr[0]);
            return cpputils::SHA256::toString(sha.digest());

#else

            return cpputils::HashFunction(std::to_string(cursor->keys[0]) + cursor->ptr2TreeOrData.dataPtr[0]);

#endif
        }
        auto child_size = (*cursor).ptr2TreeOrData.ptr2Tree.size();
        cursor->layer = cursor->ptr2TreeOrData.ptr2Tree[0]->layer + 1;
        if (child_size == 2) {

#ifndef SSL_SHA256
            Hash l = Hash((*cursor).ptr2TreeOrData.ptr2Tree[0]->hash);
            Hash r = Hash((*cursor).ptr2TreeOrData.ptr2Tree[1]->hash);
            Hash ret;
            sha256_compress(l, r, ret);
            return ret.to_string(32);
#else

            auto l = (*cursor).ptr2TreeOrData.ptr2Tree[0]->hash;
            auto r = (*cursor).ptr2TreeOrData.ptr2Tree[1]->hash;
            return cpputils::HashFunction(l + r);

#endif
        } else if (child_size == 1) {
#ifndef SSL_SHA256

            Hash l = Hash((*cursor).ptr2TreeOrData.ptr2Tree[0]->hash);
            Hash r = Hash((*cursor).ptr2TreeOrData.ptr2Tree[0]->hash);
            Hash ret;
            sha256_compress(l, r, ret);
            return l.to_string(32);
#else

            auto l = (*cursor).ptr2TreeOrData.ptr2Tree[0]->hash;
            return cpputils::HashFunction(l);

#endif
        } else {
            std::cout << "???" << std::endl;
            (*cursor).print();
            exit(-1);
        }
    }

    bool findPath(const KeyType &key, std::vector<Node *> &path);

    void lazyPointUpdate(const KeyType &key, bool countAsMiss);

    int getMaxIntChildLimit() const;

    int getMaxLeafNodeLimit() const;

    void setRoot(Node *);

public:
    MerkleBPlusTree(std::shared_ptr<ADSStore> db, uint64_t tree_id) {
        this->maxIntChildLimit = 2;
        this->maxLeafNodeLimit = 1;
        this->statistic_point = 0;
        this->statistic_batch = 0;
        this->statistic_miss = 0;
        this->root = nullptr;
        db2_ = db;
        tree_id_ = tree_id;
    }

    ~MerkleBPlusTree() {
    }

    Node *getRoot();

    std::string getRootHash();

    void display(Node *cursor);

    void display();

    void seqDisplay(Node *cursor);

    void seqDisplay();

    void nodeDisplay(Node *cursor);

    void nodeDisplay();

    bool search(const KeyType &key, std::string &result);

    bool dirtySearch(const KeyType &key, std::string &result, bool &dirty);

    void lazyBatchUpdate(std::priority_queue<Node *, std::vector<Node *>, cmp> &update);

    void insert(const KeyType &key, const std::string &value, bool immediate = false);

    bool prove(const KeyType &key, std::vector<uint8_t> &bytes);

    void build(std::vector<KeyType>::iterator st, std::vector<KeyType>::iterator ed) {
        auto size = ed - st;
        if (this->root != nullptr) {
            std::cout << "tree not empty" << std::endl;
            exit(-1);
        }

        if ((size & size - 1) != 0) {
            std::cout << "size supposed to be 2^n" << std::endl;
            exit(-1);
        }

        std::sort(st, ed);
        std::queue<Node *> q;

        Node *first = new Node;

        auto it = st;
        first->keys.push_back(*it);
        new(&first->ptr2TreeOrData.dataPtr) std::vector<std::string>;
        first->ptr2TreeOrData.dataPtr.push_back("");
        first->isLeaf = true;
        first->hash = computeHash(first, false);
        q.push(first);

        for (++it; it != ed; ++it) {
            Node *cursor = new Node;
            cursor->keys.push_back(*it);
            new(&cursor->ptr2TreeOrData.dataPtr) std::vector<std::string>;
            cursor->ptr2TreeOrData.dataPtr.push_back("");
            cursor->isLeaf = true;
            cursor->hash = computeHash(cursor, false);
            q.push(cursor);

            first->ptr2next = cursor;
            first = cursor;
        }
        std::cerr << "nodes ok" << std::endl;

        while (true) {
            if (q.size() == 1) {
                this->root = q.front();
                q.pop();
                break;
            }

            auto prev = q.front();
            q.pop();
            auto rear = q.front();
            q.pop();
            Node *parent = new Node;
            new(&parent->ptr2TreeOrData.ptr2Tree) std::vector<Node *>;
            parent->ptr2TreeOrData.ptr2Tree.push_back(prev);
            parent->ptr2TreeOrData.ptr2Tree.push_back(rear);
            parent->keys.push_back(firstLeftNode(rear)->keys[0]);
            prev->father = parent;
            rear->father = parent;
            parent->hash = computeHash(parent, false);
            q.push(parent);
        }
        std::cerr << "pointers ok" << std::endl;
    }

    void build_old(std::vector<KeyType> &keys) {
        if (this->root != nullptr) {
            std::cout << "tree not empty" << std::endl;
            exit(-1);
        }

        if ((keys.size() & keys.size() - 1) != 0) {
            std::cout << "size supposed to be 2^n" << std::endl;
            exit(-1);
        }

        std::sort(keys.begin(), keys.end());
        std::queue<Node *> q;

        Node *first = new Node;
        first->keys.push_back(keys[0]);
        new(&first->ptr2TreeOrData.dataPtr) std::vector<std::string>;
        first->ptr2TreeOrData.dataPtr.push_back("");
        first->isLeaf = true;
        first->hash = computeHash(first, false);
        q.push(first);

        for (int i = 1; i < keys.size(); i++) {
            Node *cursor = new Node;
            cursor->keys.push_back(keys[i]);
            new(&cursor->ptr2TreeOrData.dataPtr) std::vector<std::string>;
            cursor->ptr2TreeOrData.dataPtr.push_back("");
            cursor->isLeaf = true;
            cursor->hash = computeHash(cursor, false);
            q.push(cursor);

            first->ptr2next = cursor;
            first = cursor;
        }
        std::cerr << "nodes ok" << std::endl;

        while (true) {
            if (q.size() == 1) {
                this->root = q.front();
                q.pop();
                break;
            }

            auto prev = q.front();
            q.pop();
            auto rear = q.front();
            q.pop();
            Node *parent = new Node;
            new(&parent->ptr2TreeOrData.ptr2Tree) std::vector<Node *>;
            parent->ptr2TreeOrData.ptr2Tree.push_back(prev);
            parent->ptr2TreeOrData.ptr2Tree.push_back(rear);
            parent->keys.push_back(firstLeftNode(rear)->keys[0]);
            prev->father = parent;
            rear->father = parent;
            parent->hash = computeHash(parent, false);
            q.push(parent);
        }
        std::cerr << "pointers ok" << std::endl;
    }
};

#endif