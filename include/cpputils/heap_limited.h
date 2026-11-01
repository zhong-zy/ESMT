
#ifndef INDEX_MAINTAIN_HEAP_LIMITED_H
#define INDEX_MAINTAIN_HEAP_LIMITED_H
#include <iostream>
#include <stdexcept>
#include <vector>
#include "cpputils/heap.h"
#include <algorithm>

namespace cpputils {
    template<typename T>
    class HeapMinItem {
    public:
        T* item;
        int index;
    };

    template<typename T>
    class HeapLimited: public Heap<T> {
    private:
        int limit_height_ = 0;
        int limit = 0;

    public:
        HeapLimited() = default;

        explicit HeapLimited(int limit_height) {
            limit_height_ = limit_height;
            limit = (1 << limit_height_) - 1;

        }

        void set_limit_height(int limit_height) {
            limit_height_ = limit_height;
            limit = (1 << limit_height_) - 1;

        }

        void insert(T key);

        int minIndex();

        void push(T key){ insert(key);}
    };

    template<typename T>
    inline int HeapLimited<T>::minIndex() {
        if (this->array.size() != limit) {
            exit(555);
        }
            int st = (1 << (limit_height_ - 1)) - 1;
        int min_i = st;
        T min_t = this->array[st];
        for (int i = st+1, j = 1; i < limit; ++i, ++j) {
            if (this->array[i] < min_t) {
                min_t = this->array[i];
                min_i = i;
            }
        }
        return min_i;
    }

    template<typename T>
    inline void HeapLimited<T>::insert(T key)
    {
        if (limit_height_ == 0) {
            Heap<T>::insert(key);
            return;
        }

        int size = this->array.size();
        if (size < limit) {
            Heap<T>::insert(key);
            return;
        }

        int min_i = minIndex();
        if (key > this->array[min_i]) {
            Heap<T>::increaseKey(min_i, key);
        }
    }
}

#endif
