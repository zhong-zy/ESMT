
#ifndef SHRUBS_LIKE_INDEX_INDEXLETUS_H
#define SHRUBS_LIKE_INDEX_INDEXLETUS_H

#include <shared_mutex>
#include <thread>
#include "dbmaintainer/indices/index.h"
#include "dbmaintainer/indices/common/order_items.h"
#include "cpputils/sync_table_flags.h"
#include "dbmaintainer/LETUS/DMMTrie.hpp"
#include <chrono>

#include "dbmaintainer/LETUS/LSVPS.hpp"

#define SINGLE_TREAD

namespace indices {
    using namespace chrono;

    class IndexLETUS: public Index {

        letus::DMMTrie* tree;
        letus::DMMTrie* tree_copy;
        uint64_t tree_id_ = 0;

        unordered_map<KeyType, shared_mutex> data_mtx;
        unordered_map<KeyType, shared_mutex> ads_item_mtx;
        shared_mutex ads_mtx;

        double max_score = 1;
        shared_ptr<thread> ads_thread;
        AdsQueue ads_pc;

        cpputils::SyncTableFlags<KeyType, bool> dirty_hash_keys;
        cpputils::SyncTableFlagsBinary<KeyType> dirty_set;
        condition_variable ads_update_wait_cond;
        std::mutex ads_update_wait_mtx;
        uint64_t insert_cnt=0;
        cpputils::SyncTableFlags<KeyType, int64_t> last_update_ts;

    public:
        uint64_t maintainer_wait_time = 0;
        uint64_t hash_update_cnt_ = 0, hash_update_cnt_last = 0;
        uint64_t blocked_read = 0;
        uint64_t read_wait = 0;
        uint64_t process_maintenance_wait_time = 0;
        uint64_t hit_cnt = 0;
        uint64_t loss_cnt = 0;

        uint64_t pop_cnt = 0, skip_stale_cnt = 0, skip_clean_cnt = 0;
        uint64_t clean_us_sum = 0, clean_us_cnt = 0;
        uint64_t clean_us_hist[6] = {0, 0, 0, 0, 0, 0};
        bool have_last_clean_key = false;
        uint64_t current_version_ = 1;

        map<string, string> value_store_;
        map<string, tuple<uint64_t, uint64_t, uint64_t> > value_addr_;
        letus::VDLS* value_store2_;
        uint64_t key_len_ = 0;

    public:
        void ClearHash() final {
            dirty_hash_keys.clear();
        }
        uint64_t hash_update_cnt() {
            return hash_update_cnt_;
        }
        uint64_t get_blocked_read() {
            return blocked_read;
        }
        uint64_t get_read_wait() {

            return read_wait;
        }
        uint64_t get_process_maintenance_wait_time() {
            return process_maintenance_wait_time;
        }
        uint64_t get_hit_cnt() {
            return hit_cnt;
        }
        uint64_t get_loss_cnt() {
            return loss_cnt;
        }
        uint64_t dirty_size() {
            return dirty_set.get_set().size();
        }
        uint64_t ads_queue_size() {
            return ads_pc.size();
        }
        uint64_t get_ads_queue_max() { return ads_pc.len_max(); }
        uint64_t get_ads_queue_avg() { return ads_pc.len_avg(); }
        uint64_t get_dedup_merge_cnt() { return ads_pc.dedup_merge_cnt; }
        uint64_t get_dedup_update_cnt() { return ads_pc.dedup_update_cnt; }
        uint64_t get_dedup_promote_cnt() { return ads_pc.dedup_promote_cnt; }
        void reset_ads_stats() override { ads_pc.reset_len_stats(); }

        void reset_stats() override {
            hit_cnt = 0; loss_cnt = 0; blocked_read = 0; read_wait = 0;
            process_maintenance_wait_time = 0;
            pop_cnt = 0; skip_stale_cnt = 0; skip_clean_cnt = 0;
            clean_us_sum = 0; clean_us_cnt = 0; have_last_clean_key = false;
            for (int i = 0; i < 6; ++i) clean_us_hist[i] = 0;
        }

        uint64_t get_pop_cnt() final { return pop_cnt; }
        uint64_t get_skip_stale_cnt() final { return skip_stale_cnt; }
        uint64_t get_skip_clean_cnt() final { return skip_clean_cnt; }
        uint64_t get_clean_us_sum() final { return clean_us_sum; }
        uint64_t get_clean_us_cnt() final { return clean_us_cnt; }
        uint64_t get_clean_us_hist(int b) final { return (b >= 0 && b < 6) ? clean_us_hist[b] : 0; }

    public:
        IndexLETUS(shared_ptr<ADSStore> &db, uint64_t tree_id=0): tree_id_(tree_id) {
            system("pwd");

            key_len_ = to_string(workloads::init_record_count).length();

            letus::LSVPS* page_store = new letus::LSVPS("./letus_data");

            value_store2_ = new letus::VDLS("./letus_data/");
            tree = new letus::DMMTrie(0, page_store, value_store2_);
            page_store->RegisterTrie(tree);

            letus::LSVPS* page_store_cp = new letus::LSVPS("./letus_data_cp");
            letus::VDLS* value_store_cp = new letus::VDLS("./letus_data_cp/");
            tree_copy = new letus::DMMTrie(0, page_store_cp, value_store_cp);
            page_store_cp->RegisterTrie(tree_copy);

            ads_pc.name = "ads_pc";
            ads_pc.msgs_buffer_size = config::total_ops;

            dirty_hash_keys.set_default_value(config::init_dirty);
            dirty_set.set_default_value(false);
        }

        ~IndexLETUS() {
            system("rm -r letus_data/*");
            system("rm letus_data_cp/*");
        }

        void join() final {
            ads_pc.finish_produce();
            ads_pc.stop();
            ads_thread->join();
        }

        void build(vector<KeyType>::iterator st, vector<KeyType>::iterator ed) final {
#ifndef SINGLE_TREAD
            data_mtx.reserve(ed - st);
#endif
            uint64_t size = ed - st;
            ads_item_mtx.reserve(size);
            dirty_hash_keys.reserve(size);
            tree->build(st, ed);
            tree_copy->build(st, ed);
            for (auto it = st; it != ed; it++) {
                auto key = *it;
                auto skey = BuildKeyName(key, key_len_);
                value_store_[skey] = skey;
            }

            last_update_ts.reserve(size);
            last_update_ts.set_default_value(1);
            ++current_version_;

            ads_thread = make_shared<thread>([&]{
                while(config::process) {
                    if (!ads_pc.need_consume()) {
                        break;
                    }

                    auto st = steady_clock::now();
                    auto&& op = ads_pc.consume();

                    auto ed = steady_clock::now();
                    auto duration = duration_cast<milliseconds>(ed - st).count();
                    maintainer_wait_time += duration;

                    if (ads_pc.consumed_stop_msg()) {
                        break;
                    }

                    ++pop_cnt;
                    auto last_ts = last_update_ts.get(op.key);

                    if (op.time <= last_ts) {
                        ++skip_stale_cnt;
                        continue;
                    }

                    last_update_ts[op.key] = op.time;

                    if (dirty_hash_keys.get(op.key)) {
                        auto ct0 = std::chrono::steady_clock::now();
                        pointUpdateHash(op.key);
                        auto ct1 = std::chrono::steady_clock::now();
                        {
                            uint64_t us = (uint64_t) std::chrono::duration_cast<std::chrono::microseconds>(ct1 - ct0).count();
                            clean_us_sum += us; ++clean_us_cnt;
                            int b = us < 5 ? 0 : (us < 10 ? 1 : (us < 20 ? 2 : (us < 50 ? 3 : (us < 100 ? 4 : 5))));
                            ++clean_us_hist[b];
                            have_last_clean_key = true;
                        }
                        dirty_hash_keys[op.key] = false;
                        dirty_set.erase(op.key);
                        hit_cnt += (op.op == UPDATE);
                        loss_cnt += (op.op == READ);
                    } else {
                        ++skip_clean_cnt;
                    }
                    ads_update_wait_cond.notify_one();
                }
            });
        }

        void insert(KeyType &key, const string &value, bool update_hash) final {
#ifndef SINGLE_TREAD
            std::unique_lock<std::shared_mutex> lock(data_mtx[key]);
#endif
            unique_lock<shared_mutex> ads_item_lock(ads_item_mtx[key]);

            auto skey = BuildKeyName(key, key_len_);
            value_store_[skey] = value;

            auto addr = value_store2_->WriteValue(1, skey, value);
            value_addr_[skey] = addr;
            dirty_hash_keys[key] = true;
            dirty_set.insert(key);

        }

        shared_ptr<string> search(KeyType &key) final {
#ifndef SINGLE_TREAD
            std::shared_lock<std::shared_mutex> lock(data_mtx[key]);
#endif
            auto ret = make_shared<string>();

            auto skey = BuildKeyName(key, key_len_);
            auto ret1 = value_store_[skey];

            auto addr = value_addr_[skey];
            auto ret2 = value_store2_->ReadValue(addr);
            *ret = ret1;
            return ret;
        }

        void Prove(KeyType &key, int64_t ts) final {

            if (dirty_hash_keys.get(key)) {

                ++blocked_read;

                static const bool lazy_inline = (std::getenv("LAZY_INLINE") != nullptr);
                if (lazy_inline) {
                    InlineUpdateHash({key, max_score, READ, ts, 0});
                } else {
                    auto st = steady_clock::now();
                    PointUpdateHash({key, max_score, READ, ts, 0}, true);
                    auto ed = steady_clock::now();
                    auto duration = duration_cast<microseconds>(ed - st).count();
                    read_wait += duration;
                }
            }

            prove(key);
        }

        void InlineUpdateHash(FreqOrderItem op) final {
            auto last_ts = last_update_ts.get(op.key);
            if (op.time <= last_ts) return;
            last_update_ts[op.key] = op.time;
            if (dirty_hash_keys.get(op.key)) {
                pointUpdateHash(op.key);
                dirty_hash_keys[op.key] = false;
                dirty_set.erase(op.key);
                hit_cnt += (op.op == UPDATE);
                loss_cnt += (op.op == READ);
            }
        }

        void PointUpdateHash(FreqOrderItem op, bool immediate) final {

            ads_pc.produce(op);
            if (immediate) {
                auto st = steady_clock::now();
                std::unique_lock<std::mutex> lock(ads_update_wait_mtx);
                ads_update_wait_cond.wait(lock, [&] { return !dirty_hash_keys.get(op.key); });
                auto ed = steady_clock::now();
                auto duration = duration_cast<microseconds>(ed - st).count();
                process_maintenance_wait_time += duration;
            }
        }

        void BatchUpdateHash(set<KeyType> *dirty_keys = nullptr) {
            if (dirty_keys == nullptr) {
                cout << "[IndexBTree][BatchUpdateHash] dirty_keys == nullptr" << endl;
                throw "";
            }

            bool has_update = false;
            for (auto it = dirty_keys->begin(); it != dirty_keys->end(); ++it) {
                auto skey = BuildKeyName(*it, key_len_);
                tree->Put(0, current_version_, skey, value_store_[skey]);
                has_update = true;
            }
            if (has_update) {
                tree->Commit(current_version_);
                ++current_version_;
            }
            for (auto it = dirty_keys->begin(); it != dirty_keys->end(); ++it) {
                KeyType key = *it;
                dirty_hash_keys[key] = false;
                dirty_set.erase(key);
            }
        }

    public:
        void prove(KeyType &key) {

            std::vector<uint8_t> bytes;
            Hash root = tree_copy->GetRootHash(0, 1);

            auto skey = BuildKeyName(key, key_len_);

            letus::DMMTrieProof proof = tree_copy->GetProof(0, 1, skey);

        }

        void pointUpdateHash(KeyType &key) {
            std::unique_lock<std::shared_mutex> lock(ads_mtx);

            unique_lock<shared_mutex> ads_item_lock(ads_item_mtx[key]);

            auto skey = BuildKeyName(key, key_len_);

            tree->Put(0, current_version_, skey, value_store_[skey]);
            tree->Commit(current_version_);
            ++hash_update_cnt_;
            ++current_version_;

        }

        void batchUpdateHash() {

        }
    };
}

#endif