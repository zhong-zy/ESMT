
#ifndef INDEX_MAINTAIN_SMALLSET_H
#define INDEX_MAINTAIN_SMALLSET_H

#include "cstring"
#include "iostream"
#include <shared_mutex>
#define small_set_size 1048586

class SmallSet {
    uint64_t sz;

    bool *small_set;
    uint s = 0;

    std::shared_mutex *mtx;
public:
    void Init(uint64_t size) {
        this->sz = size + 10;
        this->small_set = new bool[this->sz];
        this->mtx = new shared_mutex[this->sz];
    }
    void clear() {
        memset(small_set, 0, sizeof(bool) * this->sz);
        s = 0;
    }
    void insert(uint64_t key) {
        std::unique_lock<std::shared_mutex> lock(mtx[key]);
        if (!small_set[key]) {
            small_set[key] = true;
            ++s;
        }
    }
    void erase(uint64_t key) {
        std::unique_lock<std::shared_mutex> lock(mtx[key]);
        if (small_set[key]) {
            small_set[key] = false;
            --s;
        }
    }
    bool find(uint64_t key) {
        std::shared_lock<std::shared_mutex> lock(mtx[key]);
        return small_set[key];
    }
    bool end() {
        return false;
    }
    uint size() {
        return s;
    }
};

template<typename T>
class SmallSet2{
public:
    uint64_t sz;

    T *small_set;
    uint s = 0;

    std::shared_mutex *mtx;
    void Init(uint64_t size) {
        this->sz = size + 10;
        this->small_set = new T[this->sz];
        this->mtx = new shared_mutex[this->sz];
    }
    void clear() {
        memset(small_set, 0, sizeof(T) * this->sz);
        s = 0;
    }
    void insert(uint64_t key) {
        std::unique_lock<std::shared_mutex> lock(mtx[key]);
        if (!small_set[key]) {
            small_set[key] = true;
            ++s;
        }
    }
    void erase(uint64_t key) {
        std::unique_lock<std::shared_mutex> lock(mtx[key]);
        if (small_set[key]) {
            small_set[key] = false;
            --s;
        }
    }
    bool find(uint64_t key) {
        std::shared_lock<std::shared_mutex> lock(mtx[key]);
        return small_set[key];
    }
    bool end() {
        return false;
    }
    uint size() {
        return s;
    }

    T& operator[](uint64_t key) {
        std::unique_lock<std::shared_mutex> lock(mtx[key]);
        return small_set[key];
    };
};

#endif
