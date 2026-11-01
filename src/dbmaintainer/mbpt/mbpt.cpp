#include "dbmaintainer/mbpt/mbpt.h"
#include <iostream>
#include <algorithm>
#include <vector>
#include <queue>

using namespace std;

Node *parent = nullptr;

Node::ptr::ptr() {
}

Node::ptr::~ptr() {
}

Node::Node() {
    this->isLeaf = false;
    this->isDirty = false;
    this->layer = 0;
    this->ptr2next = nullptr;
    this->father = nullptr;
}

int MerkleBPlusTree::getMaxIntChildLimit() const {
    return maxIntChildLimit;
}

int MerkleBPlusTree::getMaxLeafNodeLimit() const {
    return maxLeafNodeLimit;
}

Node *MerkleBPlusTree::getRoot() {

    return this->root;
}

string MerkleBPlusTree::getRootHash() {

    string ret = root->hash;
    return ret;
}

void MerkleBPlusTree::setRoot(Node *ptr) {
    this->root = ptr;
}

Node *MerkleBPlusTree::firstLeftNode(Node *cursor) {

    if (cursor->isLeaf)
        return cursor;
    for (int i = 0; i < cursor->ptr2TreeOrData.ptr2Tree.size(); i++)
        if (cursor->ptr2TreeOrData.ptr2Tree[i] != NULL)
            return firstLeftNode(cursor->ptr2TreeOrData.ptr2Tree[i]);

    return NULL;
}

Node **MerkleBPlusTree::findParent(Node *cursor, Node *child) {

    if (cursor->isLeaf || cursor->ptr2TreeOrData.ptr2Tree[0]->isLeaf)
        return nullptr;

    for (int i = 0; i < cursor->ptr2TreeOrData.ptr2Tree.size(); i++) {
        if (cursor->ptr2TreeOrData.ptr2Tree[i] == child) {
            parent = cursor;
        } else {

            Node *tmpCursor = cursor->ptr2TreeOrData.ptr2Tree[i];
            findParent(tmpCursor, child);
        }
    }

    return &parent;
}

void MerkleBPlusTree::insert(const KeyType &key, const string &value, bool immediate) {

    read(root);
    if (root == nullptr) {
        root = new Node;
        root->isLeaf = true;
        root->keys.push_back(key);
        new(&root->ptr2TreeOrData.dataPtr) std::vector<string>;
        root->ptr2TreeOrData.dataPtr.push_back(value);
        root->hash = computeHash(root);
        return;
    } else {

        Node *cursor = root;
        Node *parent = nullptr;
        while (!cursor->isLeaf) {
            parent = cursor;
            auto idx = std::upper_bound(cursor->keys.begin(), cursor->keys.end(), key) - cursor->keys.begin();
            read(cursor->ptr2TreeOrData.ptr2Tree[idx]);
            cursor = cursor->ptr2TreeOrData.ptr2Tree[idx];
        }

        if (key == cursor->keys[0]) {
            cursor->ptr2TreeOrData.dataPtr[0] = value;
            cursor->isDirty = true;

            db2_->Put(tree_id_, getKey(cursor), getValue(cursor));
            if (immediate) {
                lazyPointUpdate(key, false);
            }
            return;
        }

        if (cursor->keys.size() < maxLeafNodeLimit) {
            cout << "Not supposed to reach here!" << endl;
            exit(-1);

            cout << "***HERE" << endl;
            auto i = std::upper_bound(cursor->keys.begin(), cursor->keys.end(), key) - cursor->keys.begin();
            cursor->keys.push_back(key);
            cursor->ptr2TreeOrData.dataPtr.push_back(value);

            if (i != cursor->keys.size() - 1) {
                for (auto j = cursor->keys.size() - 1;
                     j > i; j--) {
                    cursor->keys[j] = cursor->keys[j - 1];
                    cursor->ptr2TreeOrData.dataPtr[j] = cursor->ptr2TreeOrData.dataPtr[j - 1];
                }

                cursor->keys[i] = key;
                cursor->ptr2TreeOrData.dataPtr[i] = value;
            }
        } else {

            vector<KeyType> virtualNode(cursor->keys);
            vector<string> virtualDataNode(cursor->ptr2TreeOrData.dataPtr);
            auto i = std::upper_bound(cursor->keys.begin(), cursor->keys.end(), key) - cursor->keys.begin();
            virtualNode.push_back(key);
            virtualDataNode.push_back(value);
            if (i != virtualNode.size() - 1) {
                for (auto j = virtualNode.size() - 1; j > i; j--) {
                    virtualNode[j] = virtualNode[j - 1];
                    virtualDataNode[j] = virtualDataNode[j - 1];
                }
                virtualNode[i] = key;
                virtualDataNode[i] = value;
            }

            Node *newLeaf = new Node;
            newLeaf->isLeaf = true;
            new(&newLeaf->ptr2TreeOrData.dataPtr) std::vector<string>;

            Node *temp = cursor->ptr2next;
            cursor->ptr2next = newLeaf;
            newLeaf->ptr2next = temp;

            cursor->keys.resize((maxLeafNodeLimit) / 2 + 1);
            cursor->ptr2TreeOrData.dataPtr.resize((maxLeafNodeLimit) / 2 + 1);
            for (int i = 0; i <= (maxLeafNodeLimit) / 2; i++) {
                cursor->keys[i] = virtualNode[i];
                cursor->ptr2TreeOrData.dataPtr[i] = virtualDataNode[i];
            }
            cursor->hash = computeHash(cursor);

            for (int i = (maxLeafNodeLimit) / 2 + 1; i < virtualNode.size(); i++) {
                newLeaf->keys.push_back(virtualNode[i]);
                newLeaf->ptr2TreeOrData.dataPtr.push_back(virtualDataNode[i]);
            }
            newLeaf->hash = computeHash(newLeaf);

            if (cursor == root) {
                Node *newRoot = new Node;
                newRoot->keys.push_back(newLeaf->keys[0]);
                new(&newRoot->ptr2TreeOrData.ptr2Tree) std::vector<Node *>;
                newRoot->ptr2TreeOrData.ptr2Tree.push_back(cursor);
                newRoot->ptr2TreeOrData.ptr2Tree.push_back(newLeaf);
                newRoot->hash = computeHash(newRoot);
                cursor->father = newRoot;
                newLeaf->father = newRoot;
                root = newRoot;

                db2_->Put(tree_id_, getKey(root), getValue(root));
            } else {
                newLeaf->father = parent;
                insertInternal(newLeaf->keys[0], &parent, &newLeaf);
            }

        }
    }
    root->hash = computeHash(root);
}

void MerkleBPlusTree::insertInternal(KeyType x, Node **cursor, Node **child) {
    read(*cursor);
    read(*child);
    if ((*cursor)->keys.size() < maxIntChildLimit - 1) {
        auto i = std::upper_bound((*cursor)->keys.begin(), (*cursor)->keys.end(), x) - (*cursor)->keys.begin();
        (*cursor)->keys.push_back(x);

        (*cursor)->ptr2TreeOrData.ptr2Tree.push_back(*child);

        if (i != (*cursor)->keys.size() - 1) {

            for (int j = (*cursor)->keys.size() - 1;
                 j > i; j--) {
                (*cursor)->keys[j] = (*cursor)->keys[j - 1];
            }
            for (int j = (*cursor)->ptr2TreeOrData.ptr2Tree.size() - 1; j > (i + 1); j--) {
                (*cursor)->ptr2TreeOrData.ptr2Tree[j] = (*cursor)->ptr2TreeOrData.ptr2Tree[j - 1];
            }
            (*cursor)->keys[i] = x;
            (*cursor)->ptr2TreeOrData.ptr2Tree[i + 1] = *child;
        }

        (*cursor)->hash = computeHash((*cursor));

        Node *p = (*cursor)->father;
        while (p != nullptr && p != root) {
            p->hash = computeHash(p);
            read(p->father);
            p = p->father;
        }
    } else {

        vector<KeyType> virtualKeyNode((*cursor)->keys);
        vector<Node *> virtualTreePtrNode((*cursor)->ptr2TreeOrData.ptr2Tree);
        auto i = std::upper_bound((*cursor)->keys.begin(), (*cursor)->keys.end(), x) - (*cursor)->keys.begin();
        virtualKeyNode.push_back(x);
        virtualTreePtrNode.push_back(*child);
        if (i != virtualKeyNode.size() - 1) {
            for (int j = virtualKeyNode.size() - 1;
                 j > i; j--) {
                virtualKeyNode[j] = virtualKeyNode[j - 1];
            }
            for (int j = virtualTreePtrNode.size() - 1; j > (i + 1); j--) {
                virtualTreePtrNode[j] = virtualTreePtrNode[j - 1];
            }
            virtualKeyNode[i] = x;
            virtualTreePtrNode[i + 1] = *child;
        }
        KeyType partitionKey;
        partitionKey = virtualKeyNode[(virtualKeyNode.size() / 2)];
        auto partitionIdx = (virtualKeyNode.size() / 2);

        (*cursor)->keys.resize(partitionIdx);
        (*cursor)->ptr2TreeOrData.ptr2Tree.resize(partitionIdx + 1);
        (*cursor)->ptr2TreeOrData.ptr2Tree.resize(partitionIdx + 1);
        for (auto i = 0; i < partitionIdx; i++) {
            (*cursor)->keys[i] = virtualKeyNode[i];
        }
        for (auto i = 0; i < partitionIdx + 1; i++) {
            (*cursor)->ptr2TreeOrData.ptr2Tree[i] = virtualTreePtrNode[i];
        }
        (*cursor)->hash = computeHash(*cursor);

        Node *newInternalNode = new Node;
        new(&newInternalNode->ptr2TreeOrData.ptr2Tree) std::vector<Node *>;
        for (auto i = partitionIdx + 1; i < virtualKeyNode.size(); i++) {
            newInternalNode->keys.push_back(virtualKeyNode[i]);
        }
        for (auto i = partitionIdx + 1; i < virtualTreePtrNode.size(); i++) {
            newInternalNode->ptr2TreeOrData.ptr2Tree.push_back(virtualTreePtrNode[i]);
        }
        newInternalNode->hash = computeHash(newInternalNode);
        (*child)->father = newInternalNode;

        if ((*cursor) == root) {
            Node *newRoot = new Node;
            newRoot->keys.push_back(partitionKey);
            new(&newRoot->ptr2TreeOrData.ptr2Tree) std::vector<Node *>;

            newRoot->ptr2TreeOrData.ptr2Tree.push_back(*cursor);
            newRoot->ptr2TreeOrData.ptr2Tree.push_back(newInternalNode);
            newRoot->hash = computeHash(newRoot);
            (*cursor)->father = newRoot;
            newInternalNode->father = newRoot;
            root = newRoot;
        } else {
            newInternalNode->father = (*cursor)->father;
            insertInternal(partitionKey, &((*cursor)->father), &newInternalNode);
        }
    }
}

bool MerkleBPlusTree::search(const KeyType &key, string &result) {
#ifdef MBPT_DISK
    read(root);
#endif
    if (root == nullptr) {
        cout << "NO Tuples Inserted yet" << endl;
        return false;
    } else {
        Node *cursor = root;
        while (!cursor->isLeaf) {

            KeyType idx = std::upper_bound(cursor->keys.begin(), cursor->keys.end(), key) - cursor->keys.begin();
#ifdef MBPT_DISK
            read(cursor->ptr2TreeOrData.ptr2Tree[idx]);
#endif
            cursor = cursor->ptr2TreeOrData.ptr2Tree[idx];
        }

        KeyType idx =
                std::lower_bound(cursor->keys.begin(), cursor->keys.end(), key) - cursor->keys.begin();

        if (idx == cursor->keys.size() || cursor->keys[idx] != key) {
            return false;
        }

        result = cursor->ptr2TreeOrData.dataPtr[idx];
        return true;
    }
}

bool MerkleBPlusTree::dirtySearch(const KeyType &key, string &result, bool &dirty) {
    read(root);
    if (root == nullptr) {
        cout << "NO Tuples Inserted yet" << endl;
        return false;
    } else {
        Node *cursor = root;
        while (!cursor->isLeaf) {

            int idx = std::upper_bound(cursor->keys.begin(), cursor->keys.end(), key) - cursor->keys.begin();
            read(cursor->ptr2TreeOrData.ptr2Tree[idx]);
            cursor = cursor->ptr2TreeOrData.ptr2Tree[idx];
        }

        int idx =
                std::lower_bound(cursor->keys.begin(), cursor->keys.end(), key) - cursor->keys.begin();

        if (idx == cursor->keys.size() || cursor->keys[idx] != key) {
            return false;
        }

        result = cursor->ptr2TreeOrData.dataPtr[idx];
        dirty = cursor->isDirty;
        return true;
    }
}

bool MerkleBPlusTree::findPath(const KeyType &key, vector<Node *> &path) {

    read(root);
    if (root == nullptr) {
        cout << "NO Tuples Inserted yet" << endl;
        return false;
    }

    Node *cursor = root;
    path.push_back(cursor);
    while (!cursor->isLeaf) {
        auto idx = std::upper_bound(cursor->keys.begin(), cursor->keys.end(), key) - cursor->keys.begin();
        path.push_back(cursor->ptr2TreeOrData.ptr2Tree[idx]);
        read(cursor->ptr2TreeOrData.ptr2Tree[idx]);
        cursor = cursor->ptr2TreeOrData.ptr2Tree[idx];
    }

    int idx =
            std::lower_bound(cursor->keys.begin(), cursor->keys.end(), key) - cursor->keys.begin();

    return true;
}

void MerkleBPlusTree::lazyPointUpdate(const KeyType &key, bool countAsMiss) {

    vector<Node *> path;
    if (findPath(key, path)) {
        std::for_each(path.rbegin(), path.rend(), [this, countAsMiss](Node *e) {

            e->hash = computeHash(e);
            e->isDirty = false;
            statistic_point++;

            db2_->Put(tree_id_, getKey(e), getValue(e));
        });
        statistic_miss += countAsMiss;
    }
}

void MerkleBPlusTree::lazyBatchUpdate(priority_queue<Node *, vector<Node *>, cmp> &update) {
    while (!update.empty()) {
        auto cursor = update.top();

        (cursor)->hash = computeHash((cursor));
        (cursor)->isDirty = false;
        update.pop();
        this->statistic_batch++;
    }
}

bool MerkleBPlusTree::prove(const KeyType &key, vector<uint8_t> &bytes) {

    read(root);
    if (root == nullptr) {
        cout << "NO Tuples Inserted yet" << endl;
        return false;
    }

    list<typename Path::Element> elements;
    Node *cursor = root;
    while (!cursor->isLeaf) {
        auto idx = std::upper_bound(cursor->keys.begin(), cursor->keys.end(), key) - cursor->keys.begin();
        if (cursor->ptr2TreeOrData.ptr2Tree.size() == 2) {
            typename Path::Element e;
            e.hash = cursor->ptr2TreeOrData.ptr2Tree[1 - idx]->hash;
            e.direction = 1 - idx == 0 ? Path::PATH_LEFT : Path::PATH_RIGHT;
            elements.push_front(std::move(e));
        }
        read(cursor->ptr2TreeOrData.ptr2Tree[idx]);
        cursor = cursor->ptr2TreeOrData.ptr2Tree[idx];
    }

    int idx =
            std::lower_bound(cursor->keys.begin(), cursor->keys.end(), key) - cursor->keys.begin();

    if (idx == cursor->keys.size() || cursor->keys[idx] != key) {
        return false;
    }

    std::make_shared<Path>(cursor->hash, std::move(elements))->serialise(bytes);
    return true;
}

void MerkleBPlusTree::display(Node *cursor) {
    if (cursor == nullptr) return;
    queue<Node *> q;
    q.push(cursor);

    while (!q.empty()) {
        auto sz = q.size();
        for (auto i = 0; i < sz; i++) {
            Node *u = q.front();
            q.pop();
            for (auto val: u->keys)
                cout << val << " ";
            cout << "|| ";
            if (!u->isLeaf) {
                for (Node *v: u->ptr2TreeOrData.ptr2Tree) {
                    q.push(v);
                }
            }
        }
        cout << endl;
    }
}

void MerkleBPlusTree::display() {
    display(root);
}

void MerkleBPlusTree::nodeDisplay(Node *cursor) {
    if (cursor == nullptr) return;
    queue<Node *> q;
    q.push(cursor);

    while (!q.empty()) {
        int sz = q.size();
        for (int i = 0; i < sz; i++) {
            Node *u = q.front();
            q.pop();
            u->print();
            if (!u->isLeaf) {
                for (Node *v: u->ptr2TreeOrData.ptr2Tree) {
                    q.push(v);
                }
            }
        }
        cout << endl << endl;
    }
}

void MerkleBPlusTree::nodeDisplay() {
    nodeDisplay(root);
}

void MerkleBPlusTree::seqDisplay(Node *cursor) {
    Node *firstLeft = firstLeftNode(cursor);

    if (firstLeft == nullptr) {
        cout << "No Data in the Database yet!" << endl;
        return;
    }
    while (firstLeft != nullptr) {
        for (int i = 0; i < firstLeft->keys.size(); i++) {
            cout << firstLeft->keys[i] << " ";
        }

        firstLeft = firstLeft->ptr2next;
    }
    cout << endl;
}

void MerkleBPlusTree::seqDisplay() {
    seqDisplay(root);
}