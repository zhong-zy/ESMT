
#ifndef INDEX_MAINTAIN_MULTI_INDEX_H
#define INDEX_MAINTAIN_MULTI_INDEX_H

#include "workloadml/new_base.h"
#include "dbmaintainer/indices/IndexMT.h"
#include "dbmaintainer/indices/IndexMPT.h"
#include "dbmaintainer/indices/IndexBTree.h"
#include "dbmaintainer/indices/IndexBucketTree.h"
#include "dbmaintainer/indices/IndexLETUS.h"
#include "dbmaintainer/indices/IndexEmpty.h"
#include <algorithm>
#include "config.h"
#include "dbmaintainer/indices/ADSStore/ADSStore.h"
#include "dbmaintainer/indices/ADSStore/ADSStoreRocksDB.h"
#include "dbmaintainer/indices/ADSStore/ADSStoreMTMemory.h"
#include <unordered_map>

using namespace workloadml;
namespace indices {
    class MultiIndexes: public Index {
    protected:
        IndexType index_type_;
        std::vector<std::shared_ptr<Index>> indexes;
        std::shared_ptr<ADSStore> db_;
        std::unordered_map<KeyType, int> key2tree;

    public:
        void ClearHash() final {
            for (auto &index: indexes) {
                index->ClearHash();
            }
        }
        uint64_t hash_update_cnt() final {
            int hash_update_cnt_ = 0;
            for (auto &index: indexes) {
                hash_update_cnt_ += index->hash_update_cnt();
            }
            return hash_update_cnt_;
        }

        virtual int route(uint64_t key) { return 0; }

        uint64_t get_blocked_read() final {
            uint64_t s = 0;
            for (auto &index: indexes) s += index->get_blocked_read();
            return s;
        }
        uint64_t get_read_wait() final {
            uint64_t s = 0;
            for (auto &index: indexes) s += index->get_read_wait();
            return s;
        }
        uint64_t get_process_maintenance_wait_time() final {
            uint64_t s = 0;
            for (auto &index: indexes) s += index->get_process_maintenance_wait_time();
            return s;
        }
        uint64_t get_hit_cnt() final {
            uint64_t s = 0;
            for (auto &index: indexes) s += index->get_hit_cnt();
            return s;
        }
        uint64_t get_loss_cnt() final {
            uint64_t s = 0;
            for (auto &index: indexes) s += index->get_loss_cnt();
            return s;
        }
        uint64_t get_pop_cnt() final {
            uint64_t s = 0;
            for (auto &index: indexes) s += index->get_pop_cnt();
            return s;
        }
        uint64_t get_skip_stale_cnt() final {
            uint64_t s = 0;
            for (auto &index: indexes) s += index->get_skip_stale_cnt();
            return s;
        }
        uint64_t get_skip_clean_cnt() final {
            uint64_t s = 0;
            for (auto &index: indexes) s += index->get_skip_clean_cnt();
            return s;
        }
        uint64_t get_clean_us_sum() final {
            uint64_t s = 0;
            for (auto &index: indexes) s += index->get_clean_us_sum();
            return s;
        }
        uint64_t get_clean_us_cnt() final {
            uint64_t s = 0;
            for (auto &index: indexes) s += index->get_clean_us_cnt();
            return s;
        }
        uint64_t get_key_gap_sum() final {
            uint64_t s = 0;
            for (auto &index: indexes) s += index->get_key_gap_sum();
            return s;
        }
        uint64_t get_clean_us_hist(int b) final {
            uint64_t s = 0;
            for (auto &index: indexes) s += index->get_clean_us_hist(b);
            return s;
        }
        uint64_t dirty_size() final {
            uint64_t s = 0;
            for (auto &index: indexes) s += index->dirty_size();
            return s;
        }
        uint64_t ads_queue_size() final {
            uint64_t s = 0;
            for (auto &index: indexes) s += index->ads_queue_size();
            return s;
        }

    public:
        MultiIndexes(std::shared_ptr<ADSStore> db, IndexType index_type = MerkleTree) {
            db_ = db;
            index_type_ = index_type;
        }

        virtual std::shared_ptr<Index> getIndex(KeyType &key) {

            auto tree_id_it = key2tree.find(key);
            int tree_id;
            if (tree_id_it != key2tree.end()) {
                tree_id = tree_id_it->second;
            } else {
                if (index_type_ == MerkleTree) {
                    std::cout << "MerkleTree does not support new keys yet" << std::endl;
                    exit(-1);
                }
                tree_id = indexes.size() - 1;
            }
            return indexes[tree_id];
        }

        void insert(KeyType &key, const std::string &value, bool update_hash) final {
            auto index = getIndex(key);
            index->insert(key, value, update_hash);
        }

        std::shared_ptr<std::string> search(KeyType &key) final {
            auto index = getIndex(key);
            return index->search(key);
        }

        void Prove(KeyType &key, int64_t ts) final {
            auto index = getIndex(key);
            index->Prove(key, ts);
        }

        void PointUpdateHash(FreqOrderItem op, bool immediate) final {
            auto index = getIndex(op.key);
            index->PointUpdateHash(op, immediate);
        }

        void BatchUpdateHash(std::set<KeyType> *dirty_keys) {}

        void join() final {
            for (auto &index: indexes) {
                index->join();
            }
        }
    };

    class Shrubs: public MultiIndexes {
        std::vector<std::pair<uint64_t, uint64_t>> group_ranges;

    public:
        std::shared_ptr<ReqScore> req_score_model_;

        Shrubs(std::shared_ptr<ADSStore> db, IndexType index_type = MerkleTree): MultiIndexes(db, index_type) {

        }

        void build(std::vector<KeyType>::iterator st, std::vector<KeyType>::iterator ed) final {
            int tree_id = 0;
            uint64_t tree_size = config::shrub_min_size;
            uint64_t key_range = ed - st;

            key2tree.clear();
            key2tree.reserve(key_range);
            uint64_t sti = 0, edi = sti + tree_size;
            std::shared_ptr<Index> index;
            while (edi < key_range) {
                if (index_type_ == MerkleTree) {
                    index = std::make_shared<IndexMT>(db_, tree_id);
                }
                auto current_st = st + sti;
                auto current_ed = st + edi;
                index->build(current_st, current_ed);
                for (auto it = current_st; it != current_ed; ++it) {
                    key2tree[*it] = group_ranges.size();
                }

                indexes.emplace_back(index);
                group_ranges.push_back({sti, edi});

                tree_size <<= 1;
                sti = edi;
                edi = sti + tree_size;
                ++tree_id;
            }

            if (index_type_ == MerkleTree) {
                index = std::make_shared<IndexMT>(db_, tree_id);

                tree_size = 1;
                while (tree_size < key_range - sti) {
                    tree_size <<= 1;
                }
                KeyType max_key = 0;
                std::vector<KeyType> last_keys;
                last_keys.resize(tree_size);
                uint64_t i = 0;
                for (auto it = st + sti; it != ed; ++it, ++i) {
                    max_key = std::max(*it, max_key);
                    last_keys[i] = *it;
                    key2tree[*it] = group_ranges.size();
                }
                for (++max_key ; i < tree_size; ++i, ++max_key) {
                    last_keys[i] = max_key;
                }

                index->build(last_keys.begin(), last_keys.end());
            }
            indexes.emplace_back(index);
            group_ranges.push_back({sti, key_range});
            ++tree_id;
        }
    };

    class RandomPartitionIndex: public MultiIndexes{

    public:
        RandomPartitionIndex(std::shared_ptr<ADSStore> db, IndexType index_type = MerkleTree): MultiIndexes(db, index_type) {

        }

        void build(std::vector<KeyType>::iterator st, std::vector<KeyType>::iterator ed) final {
            std::vector<std::vector<KeyType>> init_groups(config::tree_cnt);
            for (auto i = st; i != ed; ++i) {
                std::size_t hash = std::hash<KeyType>{}(*i);
                int group_index = hash % config::tree_cnt;
                init_groups[group_index].push_back(*i);
                key2tree[*i] = group_index;
            }

            int tree_id = 0;
            for (auto &group: init_groups) {
                std::shared_ptr<Index> index;
                if (index_type_ == MerkleTree) {
                    index = std::make_shared<IndexMT>(db_, tree_id);
                }
                index->build(group.begin(), group.end());
                indexes.emplace_back(index);
                ++tree_id;
            }
        }
    };

    class ShardPartitionIndex: public MultiIndexes {
    public:
        ShardPartitionIndex(std::shared_ptr<ADSStore> db, IndexType index_type = MerkleTree)
                : MultiIndexes(db, index_type) {}

    private:
        KeyType min_key_ = 0;
        double span_ = 1.0;
        int shard_cnt_ = 1;
        int shard_mode_ = 0;

        static uint64_t mix64(uint64_t x) {
            x += 0x9e3779b97f4a7c15ULL;
            x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
            x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
            return x ^ (x >> 31);
        }

        std::shared_ptr<Index> make_sub_index(uint64_t tree_id) {
            std::shared_ptr<ADSStore> db = db_;
            switch (index_type_) {
                case MerkleTree:
                    return std::make_shared<IndexMT>(db, tree_id);
                case MBT: {
                    auto d = std::shared_ptr<ADSStore>(nullptr);
                    return std::make_shared<IndexBTree>(d);
                }
                case MPT: {
                    auto d = std::shared_ptr<ADSStore>(nullptr);
                    return std::make_shared<IndexMPT>(d);
                }
                case BucketTree: {
                    auto d = std::shared_ptr<ADSStore>(nullptr);
                    return std::make_shared<IndexBucketTree>(d);
                }
                case LETUS: {
                    auto d = std::shared_ptr<ADSStore>(nullptr);
                    return std::make_shared<IndexLETUS>(d);
                }
                case EMPTY: {
                    auto d = std::shared_ptr<ADSStore>(nullptr);
                    return std::make_shared<IndexEmpty>(d);
                }
            }
            return std::make_shared<IndexMT>(db, tree_id);
        }

        int route(KeyType key) override {
            int S = shard_cnt_;
            int sid = 0;
            if (shard_mode_ == 1) {
                sid = static_cast<int>((static_cast<double>(key - min_key_) * S) / span_);
                if (sid >= S) sid = S - 1;
                if (sid < 0) sid = 0;
            } else {
                sid = static_cast<int>(mix64(static_cast<uint64_t>(key)) % static_cast<uint64_t>(S));
            }
            return sid;
        }

    public:
        void build(std::vector<KeyType>::iterator st, std::vector<KeyType>::iterator ed) final {
            shard_cnt_ = config::shard_cnt > 0 ? config::shard_cnt : 1;
            shard_mode_ = config::shard_mode;
            int S = shard_cnt_;
            uint64_t n = static_cast<uint64_t>(ed - st);
            std::vector<std::vector<KeyType>> groups(S);
            for (auto &g: groups) g.reserve(n / static_cast<uint64_t>(S) + 1);

            if (st != ed) {
                min_key_ = *std::min_element(st, ed);
                KeyType max_key = *std::max_element(st, ed);
                span_ = static_cast<double>(max_key - min_key_) + 1.0;
            }
            for (auto it = st; it != ed; ++it) {
                groups[route(*it)].push_back(*it);
            }
            std::cout << "[shard] S=" << S << " mode=" << (shard_mode_ == 1 ? "range" : "hash")
                      << " keys=" << n << std::endl;
            for (int sid = 0; sid < S; ++sid) {
                auto index = make_sub_index(static_cast<uint64_t>(sid));
                index->build(groups[sid].begin(), groups[sid].end());
                std::cout << "[shard] shard " << sid << " keys=" << groups[sid].size() << std::endl;
                indexes.emplace_back(index);
            }
        }

        std::shared_ptr<Index> getIndex(KeyType &key) final {
            return indexes[route(key)];
        }
    };
}

#if 0
[[deprecated]] void example() {

    auto db = make_shared<ADSStoreRocksDB>();

    auto index = new indices::Shrubs(db);
}
#endif

#endif
