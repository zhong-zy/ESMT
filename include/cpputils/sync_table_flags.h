
#ifndef SHRUBS_LIKE_INDEX_SYNC_TABLE_FLAGS_H
#define SHRUBS_LIKE_INDEX_SYNC_TABLE_FLAGS_H

#include <unordered_map>
#include <unordered_set>
#include <shared_mutex>

namespace cpputils {
    template<typename K, typename F>
    class SyncTableFlags {
        std::unordered_map<K, F> flag_table;
        F default_value_;

        std::shared_mutex mtx;
    public:
        SyncTableFlags() = default;
        void set_default_value(F default_value) {
            default_value_ = default_value;
        }
        SyncTableFlags(uint64_t size, F default_value) {
            flag_table.reserve(size);
            default_value_ = default_value;
        }
        void reserve(uint64_t size) {
            flag_table.reserve(size);
        }
        F& operator[](K &key) {
            std::unique_lock<std::shared_mutex> lock(mtx);

            return flag_table[key];
        }
        const F& get(K &key) {
            std::shared_lock<std::shared_mutex> lock(mtx);

            if (flag_table.find(key) == flag_table.end()) return default_value_;
            return flag_table[key];
        }

        void clear() {
            std::unique_lock<std::shared_mutex> lock(mtx);
            flag_table.clear();
        }
    };

    template<typename K>
    class SyncTableFlagsBinary {
        std::unordered_set<K> flag_set;
        bool default_value_;

        std::shared_mutex mtx;
    public:
        SyncTableFlagsBinary() = default;
        void set_default_value(bool default_value) {
            default_value_ = default_value;
        }
        SyncTableFlagsBinary(uint64_t size, bool default_value) {
            flag_set.reserve(size);
            default_value_ = default_value;
        }
        void reserve(uint64_t size) {
            flag_set.reserve(size);
        }
        const bool get(K &key) {
            std::shared_lock<std::shared_mutex> lock(mtx);
            if (flag_set.find(key) == flag_set.end()) return default_value_;
            return !default_value_;
        }
        void insert(K &key) {
            std::unique_lock<std::shared_mutex> lock(mtx);
            flag_set.insert(key);
        }
        void erase(K &key) {
            std::unique_lock<std::shared_mutex> lock(mtx);
            flag_set.erase(key);
        }
        void lock() {
            mtx.lock();
        }
        void unlock() {
            mtx.unlock();
        }
        void lock_shared() {
            mtx.lock_shared();
        }
        void unlock_shared() {
            mtx.unlock_shared();
        }
        std::unordered_set<K>& get_set() {
            return flag_set;
        }
        void clear() {
            std::unique_lock<std::shared_mutex> lock(mtx);
            flag_set.clear();
        }
    };
}

#endif
