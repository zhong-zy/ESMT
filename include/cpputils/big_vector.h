
#ifndef INDEX_MAINTAIN_BIG_VECTOR_H
#define INDEX_MAINTAIN_BIG_VECTOR_H

#include <iostream>

namespace cpputils {
    template<typename T>
    class BigVector {
        int index = 0;
        void *vector;
    public:
        void reserve(int size) {
            vector = malloc(size * sizeof(T));
        }
        void resize(int size) {
            reserve(size);
            index = size;
        }
        void push_back(T data) {
            ((T*)vector)[index] = data;
            ++index;
        }
        void clear() {
            index = 0;
        }
        T& operator[](uint64_t i) {
           return ((T*)vector)[i];
        }
        int size() {
            return index;
        }
        T* data() {
            return ((T*)vector);
        }
    };
}

#endif
