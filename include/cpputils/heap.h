
#ifndef INDEX_MAINTAIN_HEAP_H
#define INDEX_MAINTAIN_HEAP_H
#include <iostream>
#include <stdexcept>
#include <vector>

namespace cpputils {

    template<typename T>
    class Heap {
    protected:

        std::vector<T> array;

        void heapify(int i);

    public:

        Heap() = default;
        virtual ~Heap() = default;

        virtual void insert(T key);

        T extractMax();

        T getMax() const;

        void deleteKey(int i);

        void increaseKey(int i, T newValue);

        void decreaseKey(int i, T newValue);

        void printHeap() const;

        size_t size() {return array.size();}

        bool empty() {return array.size() == 0;}

        T top() const {return getMax();}

        void pop() {extractMax();}
    };

    template<typename T>
    inline void Heap<T>::heapify(int i)
    {
        int largest = i;
        int left = 2 * i + 1;
        int right = 2 * i + 2;
        int size = array.size();

        if (left < size && array[left] > array[largest])
            largest = left;

        if (right < size && array[right] > array[largest])
            largest = right;

        if (largest != i) {
            std::swap(array[i], array[largest]);
            heapify(largest);
        }
    }

    template<typename T>
    inline void Heap<T>::insert(T key)
    {
        array.push_back(key);
        int i = array.size() - 1;

        while (i != 0 && array[(i - 1) / 2] < array[i]) {
            std::swap(array[i], array[(i - 1) / 2]);
            i = (i - 1) / 2;
        }
    }

    template<typename T>
    inline T Heap<T>::extractMax()
    {
        if (array.size() <= 0) {
            throw std::underflow_error("Heap underflow");
        }

        if (array.size() == 1) {
            T root = array[0];
            array.pop_back();
            return root;
        }

        T root = array[0];
        array[0] = array.back();
        array.pop_back();
        heapify(0);

        return root;
    }

    template<typename T>
    inline T Heap<T>::getMax() const
    {
        if (array.size() <= 0) {
            throw std::underflow_error("Heap is empty");
        }
        return array[0];
    }

    template<typename T>
    inline void Heap<T>::deleteKey(int i)
    {
        if (i >= array.size()) {
            throw std::out_of_range("Invalid index");
        }

        array[i] = array.back();
        array.pop_back();
        heapify(i);
    }

    template<typename T>
    inline void Heap<T>::increaseKey(int i, T newValue)
    {
        if (i >= array.size() || array[i] >= newValue) {
            throw std::invalid_argument(
                    "Invalid index or new value is not greater");
        }

        array[i] = newValue;
        while (i != 0 && array[(i - 1) / 2] < array[i]) {
            std::swap(array[i], array[(i - 1) / 2]);
            i = (i - 1) / 2;
        }
    }

    template<typename T>
    inline void Heap<T>::decreaseKey(int i, T newValue)
    {
        int size = array.size();
        if (i >= size || array[i] <= newValue) {
            throw std::invalid_argument(
                    "Invalid index or new value is not greater");
        }

        array[i] = newValue;
        int left_i = i * 2 + 1;
        int right_i = i * 2 + 2;
        while (left_i < size) {
            if (right_i >= size) {
                std::swap(array[i], array[left_i]);
                break;
            }

            T& left = array[i * 2 + 1];
            T& right = array[i * 2 + 2];
            if (left > right) {
                std::swap(array[i], left);
                i = i * 2 + 1;
            } else {
                std::swap(array[i], right);
                i = i * 2 + 2;
            }
            left_i = i * 2 + 1;
            right_i = i * 2 + 2;
        }
    }

    template<typename T>
    inline void Heap<T>::printHeap() const
    {
        int i = 0;
        int j = 1;
        for (int val : array) {
            std::cout << val << " ";
            ++i;
            if (i == j) {
                std::cout << std::endl;
                j <<= 1;
                i = 0;
            }
        }
        std::cout << std::endl;
    }
}

#endif
