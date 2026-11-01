
#include "dbmaintainer/mbpt/bpt.h"
#include <queue>

using namespace std;

BPTNode* bpt_parent = NULL;

BPTNode::ptr::ptr() {
}

BPTNode::ptr::~ptr() {
}

BPTNode::BPTNode() {
    this->isLeaf = false;
    this->isDirty = false;
    this->ptr2next = NULL;
}

BPlusTree::BPlusTree() {

    this->maxIntChildLimit = 2;
    this->maxLeafNodeLimit = 1;
    this->root = NULL;
}

BPlusTree::BPlusTree(int degreeInternal, int degreeLeaf) {
    this->maxIntChildLimit = degreeInternal;
    this->maxLeafNodeLimit = degreeLeaf;
    this->root = NULL;
}

int BPlusTree::getMaxIntChildLimit() {
    return maxIntChildLimit;
}

int BPlusTree::getMaxLeafNodeLimit() {
    return maxLeafNodeLimit;
}

BPTNode* BPlusTree::getRoot() {
    return this->root;
}

void BPlusTree::setRoot(BPTNode *ptr) {
    this->root = ptr;
}

BPTNode* BPlusTree::firstLeftNode(BPTNode* cursor) {
    if (cursor->isLeaf)
        return cursor;
    for (int i = 0; i < cursor->ptr2TreeOrData.ptr2Tree.size(); i++)
        if (cursor->ptr2TreeOrData.ptr2Tree[i] != NULL)
            return firstLeftNode(cursor->ptr2TreeOrData.ptr2Tree[i]);

    return NULL;
}

BPTNode** BPlusTree::findParent(BPTNode* cursor, BPTNode* child) {

    if (cursor->isLeaf || cursor->ptr2TreeOrData.ptr2Tree[0]->isLeaf)
        return NULL;

    for (int i = 0; i < cursor->ptr2TreeOrData.ptr2Tree.size(); i++) {
        if (cursor->ptr2TreeOrData.ptr2Tree[i] == child) {
            bpt_parent = cursor;
        } else {

            BPTNode* tmpCursor = cursor->ptr2TreeOrData.ptr2Tree[i];
            findParent(tmpCursor, child);
        }
    }

    return &bpt_parent;
}

void BPlusTree::insert(const BPTKey &key, const string &value) {

    if (root == NULL) {
        root = new BPTNode;
        root->isLeaf = true;
        root->keys.push_back(key);
        new (&root->ptr2TreeOrData.dataPtr) std::vector<string>;

        root->ptr2TreeOrData.dataPtr.push_back(value);

        return;
    } else {
        BPTNode* cursor = root;
        BPTNode* bpt_parent = NULL;

        while (cursor->isLeaf == false) {
            bpt_parent = cursor;
            int idx = std::upper_bound(cursor->keys.begin(), cursor->keys.end(), key) - cursor->keys.begin();
            cursor = cursor->ptr2TreeOrData.ptr2Tree[idx];
        }

        if (key == cursor->keys[0]) {
            cursor->ptr2TreeOrData.dataPtr[0] = value;
            cursor->isDirty = true;
            return;
        }

        if (cursor->keys.size() < maxLeafNodeLimit) {

            int i = std::upper_bound(cursor->keys.begin(), cursor->keys.end(), key) - cursor->keys.begin();
            cursor->keys.push_back(key);
            cursor->ptr2TreeOrData.dataPtr.push_back(value);

            if (i != cursor->keys.size() - 1) {
                for (int j = cursor->keys.size() - 1; j > i; j--) {
                    cursor->keys[j] = cursor->keys[j - 1];
                    cursor->ptr2TreeOrData.dataPtr[j] = cursor->ptr2TreeOrData.dataPtr[j - 1];
                }

                cursor->keys[i] = key;
                cursor->ptr2TreeOrData.dataPtr[i] = value;
            }

        } else {

            vector<BPTKey> virtualNode(cursor->keys);
            vector<string > virtualDataNode(cursor->ptr2TreeOrData.dataPtr);

            int i = std::upper_bound(cursor->keys.begin(), cursor->keys.end(), key) - cursor->keys.begin();

            virtualNode.push_back(key);
            virtualDataNode.push_back(value);

            if (i != virtualNode.size() - 1) {
                for (int j = virtualNode.size() - 1; j > i; j--) {
                    virtualNode[j] = virtualNode[j - 1];
                    virtualDataNode[j] = virtualDataNode[j - 1];
                }

                virtualNode[i] = key;
                virtualDataNode[i] = value;
            }

            BPTNode* newLeaf = new BPTNode;
            newLeaf->isLeaf = true;
            new (&newLeaf->ptr2TreeOrData.dataPtr) std::vector<string>;

            BPTNode* temp = cursor->ptr2next;
            cursor->ptr2next = newLeaf;
            newLeaf->ptr2next = temp;

            cursor->keys.resize((maxLeafNodeLimit) / 2 +1);
            cursor->ptr2TreeOrData.dataPtr.resize((maxLeafNodeLimit) / 2 +1);
            for (int i = 0; i <= (maxLeafNodeLimit) / 2; i++) {
                cursor->keys[i] = virtualNode[i];
                cursor->ptr2TreeOrData.dataPtr[i] = virtualDataNode[i];
            }

            for (int i = (maxLeafNodeLimit) / 2 + 1; i < virtualNode.size(); i++) {
                newLeaf->keys.push_back(virtualNode[i]);
                newLeaf->ptr2TreeOrData.dataPtr.push_back(virtualDataNode[i]);
            }

            if (cursor == root) {

                BPTNode* newRoot = new BPTNode;
                newRoot->keys.push_back(newLeaf->keys[0]);
                new (&newRoot->ptr2TreeOrData.ptr2Tree) std::vector<BPTNode*>;
                newRoot->ptr2TreeOrData.ptr2Tree.push_back(cursor);
                newRoot->ptr2TreeOrData.ptr2Tree.push_back(newLeaf);
                root = newRoot;

            } else {

                insertInternal(newLeaf->keys[0], &bpt_parent, &newLeaf);
            }
        }
    }
}

void BPlusTree::insertInternal(BPTKey x, BPTNode** cursor, BPTNode** child) {
    if ((*cursor)->keys.size() < maxIntChildLimit - 1) {

        int i = std::upper_bound((*cursor)->keys.begin(), (*cursor)->keys.end(), x) - (*cursor)->keys.begin();
        (*cursor)->keys.push_back(x);

        (*cursor)->ptr2TreeOrData.ptr2Tree.push_back(*child);

        if (i != (*cursor)->keys.size() - 1) {

            for (int j = (*cursor)->keys.size() - 1; j > i; j--) {
                (*cursor)->keys[j] = (*cursor)->keys[j - 1];
            }

            for (int j = (*cursor)->ptr2TreeOrData.ptr2Tree.size() - 1; j > (i + 1); j--) {
                (*cursor)->ptr2TreeOrData.ptr2Tree[j] = (*cursor)->ptr2TreeOrData.ptr2Tree[j - 1];
            }

            (*cursor)->keys[i] = x;
            (*cursor)->ptr2TreeOrData.ptr2Tree[i + 1] = *child;
        }

    } else {

        vector<BPTKey> virtualKeyNode((*cursor)->keys);
        vector<BPTNode*> virtualTreePtrNode((*cursor)->ptr2TreeOrData.ptr2Tree);

        int i = std::upper_bound((*cursor)->keys.begin(), (*cursor)->keys.end(), x) - (*cursor)->keys.begin();
        virtualKeyNode.push_back(x);
        virtualTreePtrNode.push_back(*child);

        if (i != virtualKeyNode.size() - 1) {
            for (int j = virtualKeyNode.size() - 1; j > i; j--) {
                virtualKeyNode[j] = virtualKeyNode[j - 1];
            }

            for (int j = virtualTreePtrNode.size() - 1; j > (i + 1); j--) {
                virtualTreePtrNode[j] = virtualTreePtrNode[j - 1];
            }

            virtualKeyNode[i] = x;
            virtualTreePtrNode[i + 1] = *child;
        }

        BPTKey partitionKey;
        partitionKey = virtualKeyNode[(virtualKeyNode.size() / 2)];
        int partitionIdx = (virtualKeyNode.size() / 2);

        (*cursor)->keys.resize(partitionIdx);
        (*cursor)->ptr2TreeOrData.ptr2Tree.resize(partitionIdx + 1);
        (*cursor)->ptr2TreeOrData.ptr2Tree.resize(partitionIdx + 1);
        for (int i = 0; i < partitionIdx; i++) {
            (*cursor)->keys[i] = virtualKeyNode[i];
        }

        for (int i = 0; i < partitionIdx + 1; i++) {
            (*cursor)->ptr2TreeOrData.ptr2Tree[i] = virtualTreePtrNode[i];
        }

        BPTNode* newInternalNode = new BPTNode;
        new (&newInternalNode->ptr2TreeOrData.ptr2Tree) std::vector<BPTNode*>;

        for (int i = partitionIdx + 1; i < virtualKeyNode.size(); i++) {
            newInternalNode->keys.push_back(virtualKeyNode[i]);
        }

        for (int i = partitionIdx + 1; i < virtualTreePtrNode.size(); i++) {
            newInternalNode->ptr2TreeOrData.ptr2Tree.push_back(virtualTreePtrNode[i]);
        }

        if ((*cursor) == root) {

            BPTNode* newRoot = new BPTNode;
            newRoot->keys.push_back(partitionKey);
            new (&newRoot->ptr2TreeOrData.ptr2Tree) std::vector<BPTNode*>;
            newRoot->ptr2TreeOrData.ptr2Tree.push_back(*cursor);

            newRoot->ptr2TreeOrData.ptr2Tree.push_back(newInternalNode);

            root = newRoot;

        } else {

            insertInternal(partitionKey, findParent(root, *cursor), &newInternalNode);
        }
    }
}

bool BPlusTree::search(const BPTKey &key, string &result) {
    if (root == NULL) {
        cout << "NO Tuples Inserted yet" << endl;
        return false;
    } else {
        BPTNode *cursor = root;
        while (cursor->isLeaf == false) {

            int idx = std::upper_bound(cursor->keys.begin(), cursor->keys.end(), key) - cursor->keys.begin();
            cursor = cursor->ptr2TreeOrData.ptr2Tree[idx];
        }

        int idx =
                std::lower_bound(cursor->keys.begin(), cursor->keys.end(), key) - cursor->keys.begin();

        if (idx == cursor->keys.size() || cursor->keys[idx] != key) {
            return false;
        }

        result = cursor->ptr2TreeOrData.dataPtr[0];
        return true;
    }
}

void BPlusTree::display(BPTNode* cursor) {

    if (cursor == NULL) return;
    queue<BPTNode*> q;
    q.push(cursor);

    while (!q.empty()) {
        int sz = q.size();
        for (int i = 0; i < sz; i++) {
            BPTNode* u = q.front(); q.pop();

            for (auto val : u->keys)
                cout << val << " ";

            cout << "|| ";

            if (u->isLeaf != true) {
                for (BPTNode* v : u->ptr2TreeOrData.ptr2Tree) {
                    q.push(v);
                }
            }
        }
        cout << endl;
    }
}

void BPlusTree::seqDisplay(BPTNode* cursor) {
    BPTNode* firstLeft = firstLeftNode(cursor);

    if (firstLeft == NULL) {
        cout << "No Data in the Database yet!" << endl;
        return;
    }
    while (firstLeft != NULL) {
        for (int i = 0; i < firstLeft->keys.size(); i++) {
            cout << firstLeft->keys[i] << " ";
        }

        firstLeft = firstLeft->ptr2next;
    }
    cout << endl;
}