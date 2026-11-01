
#ifndef MBPT_BPT_H
#define MBPT_BPT_H

#include <iostream>
#include <vector>
#include <algorithm>
#include <string>

typedef unsigned long long BPTKey;

class BPTNode {

public:
    bool isLeaf;
    bool isDirty;
    std::vector<BPTKey> keys;

    BPTNode* ptr2next;
    union ptr {
        std::vector<BPTNode*> ptr2Tree;
        std::vector<std::string> dataPtr;

        ptr();
        ~ptr();
    } ptr2TreeOrData;

    friend class BPlusTree;
public:
    BPTNode();
};

class BPlusTree {

private:
    int maxIntChildLimit;
    int maxLeafNodeLimit;
    BPTNode* root;
    void insertInternal(BPTKey x, BPTNode** cursor, BPTNode** child);
    BPTNode** findParent(BPTNode* cursor, BPTNode* child);
    BPTNode* firstLeftNode(BPTNode* cursor);

public:
    BPlusTree();
    BPlusTree(int degreeInternal, int degreeLeaf);
    BPTNode* getRoot();
    int getMaxIntChildLimit();
    int getMaxLeafNodeLimit();
    void setRoot(BPTNode *);
    void display(BPTNode* cursor);
    void seqDisplay(BPTNode* cursor);
    bool search(const BPTKey &key, std::string& result);
    void insert(const BPTKey &key,const std::string &value);
};

#endif
