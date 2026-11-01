
#ifndef SHRUBS_LIKE_INDEX_INDEXBTREE_H
#define SHRUBS_LIKE_INDEX_INDEXBTREE_H

#include <shared_mutex>
#include <thread>
#include "dbmaintainer/indices/index.h"
#include "dbmaintainer/indices/common/order_items.h"
#include "cpputils/sync_table_flags.h"
#include "dbmaintainer/mbt/merkle_btree.h"
#include "dbmaintainer/pk_stats.h"
#include <chrono>

#define SINGLE_TREAD

namespace indices {
    class IndexBTree: public Index {
        std::shared_ptr<MerkleBTree> tree;
        uint64_t tree_id_ = 0;

        std::unordered_map<KeyType, std::shared_mutex> data_mtx;
        std::unordered_map<KeyType, std::shared_mutex> ads_item_mtx;
        std::shared_mutex ads_mtx;

        double max_score = 1;
        std::shared_ptr<std::thread> ads_thread;
        AdsQueue ads_pc;
        std::unordered_map<uint32_t, int64_t> coalesce_last_ts;

        cpputils::SyncTableFlags<KeyType, bool> dirty_hash_keys;
        cpputils::SyncTableFlagsBinary<KeyType> dirty_set;
        std::condition_variable ads_update_wait_cond;
        std::mutex ads_update_wait_mtx;
        uint64_t insert_cnt=0;
        cpputils::SyncTableFlags<KeyType, int64_t> last_update_ts;

    public:
        uint64_t maintainer_wait_time = 0;
        uint64_t hash_update_cnt_ = 0, hash_update_cnt_last = 0;
        uint64_t blocked_read = 0;

        uint64_t prove_us_sum = 0, prove_us_cnt = 0;
        uint64_t read_wait = 0;
        uint64_t hit_cnt = 0;
        uint64_t loss_cnt = 0;
        uint64_t process_maintenance_wait_time = 0;

        uint64_t pop_cnt = 0;
        uint64_t skip_stale_cnt = 0;
        uint64_t skip_clean_cnt = 0;
        uint64_t clean_us_sum = 0, clean_us_cnt = 0, key_gap_sum = 0;
        uint64_t clean_us_hist[6] = {0, 0, 0, 0, 0, 0};
        uint32_t last_clean_key = 0;
        bool have_last_clean_key = false;

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
        uint64_t get_prove_us_sum() final { return prove_us_sum; }
        uint64_t get_prove_us_cnt() final { return prove_us_cnt; }
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
        uint64_t get_pop_cnt() final { return pop_cnt; }
        uint64_t get_skip_stale_cnt() final { return skip_stale_cnt; }
        uint64_t get_skip_clean_cnt() final { return skip_clean_cnt; }
        uint64_t get_clean_us_sum() final { return clean_us_sum; }
        uint64_t get_clean_us_cnt() final { return clean_us_cnt; }
        uint64_t get_key_gap_sum() final { return key_gap_sum; }
        uint64_t get_clean_us_hist(int b) final { return (b >= 0 && b < 6) ? clean_us_hist[b] : 0; }
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

        void reset_stats() final {
            hit_cnt = 0; loss_cnt = 0; blocked_read = 0; read_wait = 0;
            pop_cnt = 0; skip_stale_cnt = 0; skip_clean_cnt = 0;
            clean_us_sum = 0; clean_us_cnt = 0; key_gap_sum = 0;
            prove_us_sum = 0; prove_us_cnt = 0;
            for (int i = 0; i < 6; ++i) clean_us_hist[i] = 0;
            have_last_clean_key = false;
        }

    public:
        IndexBTree(std::shared_ptr<ADSStore> &db, uint64_t tree_id=0): tree_id_(tree_id) {

            tree = std::make_shared<MerkleBTree>();
            ads_pc.name = "ads_pc";
            ads_pc.msgs_buffer_size = config::total_ops;

            dirty_hash_keys.set_default_value(config::init_dirty);
            dirty_set.set_default_value(false);
        }

        void join() final {
            ads_pc.finish_produce();
            ads_pc.stop();
            ads_thread->join();
        }

        void build(std::vector<KeyType>::iterator st, std::vector<KeyType>::iterator ed) final {
#ifndef SINGLE_TREAD
            data_mtx.reserve(ed - st);
#endif
            uint64_t size = ed - st;
            ads_item_mtx.reserve(size);
            dirty_hash_keys.reserve(size);
            tree->build(st, ed);

            last_update_ts.reserve(size);
            last_update_ts.set_default_value(-1);

            ads_thread = std::make_shared<std::thread>([&]{
                while(config::process) {
                    if (!ads_pc.need_consume()) {
                        break;
                    }

                    auto st = std::chrono::steady_clock::now();
                    auto&& op = ads_pc.consume();

                    auto ed = std::chrono::steady_clock::now();
                    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(ed - st).count();
                    maintainer_wait_time += duration;

                    if (ads_pc.consumed_stop_msg()) {
                        break;
                    }

            auto last_ts = last_update_ts.get(op.key);
            ++pop_cnt;
            if (config::ads_coalesce) {
                auto ct = coalesce_last_ts.find(op.key);
                if (ct != coalesce_last_ts.end() && ct->second > op.time) { ++skip_stale_cnt; continue; }
            }

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
                            clean_us_sum += us;
                            ++clean_us_cnt;
                            int b = us < 5 ? 0 : (us < 10 ? 1 : (us < 20 ? 2 : (us < 50 ? 3 : (us < 100 ? 4 : 5))));
                            ++clean_us_hist[b];
                            if (have_last_clean_key) {
                                key_gap_sum += (op.key > last_clean_key ? (uint64_t)(op.key - last_clean_key)
                                                                        : (uint64_t)(last_clean_key - op.key));
                            }
                            last_clean_key = op.key;
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

        void insert(KeyType &key, const std::string &value, bool update_hash) final {
#ifndef SINGLE_TREAD
            std::unique_lock<std::shared_mutex> lock(data_mtx[key]);
#endif
            std::unique_lock<std::shared_mutex> ads_item_lock(ads_item_mtx[key]);

            tree->insert(key, const_cast<std::string &>(value), false);
            dirty_hash_keys[key] = true;
            dirty_set.insert(key);

        }

        std::shared_ptr<std::string> search(KeyType &key) final {
#ifndef SINGLE_TREAD
            std::shared_lock<std::shared_mutex> lock(data_mtx[key]);
#endif
            auto ret = std::make_shared<std::string>();

            auto [ret1, found] = tree->Get(key);
            *ret = ret1;
            return ret;
        }

        void Prove(KeyType &key, int64_t ts) final {

            pkstats::clear_cleaned(key);

            if (dirty_hash_keys.get(key)) {

                ++blocked_read;
                pkstats::note_blocked(key, ts, (int64_t) ads_pc.size());

                static const bool lazy_inline = (std::getenv("LAZY_INLINE") != nullptr);
                if (lazy_inline) {
                    InlineUpdateHash({key, max_score, READ, ts, 0});
                } else {
                    auto st = std::chrono::steady_clock::now();
                    PointUpdateHash({key, max_score, READ, ts, 0}, true);
                    auto ed = std::chrono::steady_clock::now();
                    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(ed - st).count();
                    read_wait += duration;
                }
            }

            {
                auto pst = std::chrono::steady_clock::now();
                prove(key);
                auto ped = std::chrono::steady_clock::now();
                prove_us_sum += (uint64_t) std::chrono::duration_cast<std::chrono::microseconds>(ped - pst).count();
                ++prove_us_cnt;
            }
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
            if (config::ads_coalesce) {
                std::unique_lock<std::mutex> lk(ads_update_wait_mtx);
                coalesce_last_ts[op.key] = op.time;
            }
            if (immediate) {
                auto st = std::chrono::steady_clock::now();
                std::unique_lock<std::mutex> lock(ads_update_wait_mtx);
                ads_update_wait_cond.wait(lock, [&] { return !dirty_hash_keys.get(op.key); });
                auto ed = std::chrono::steady_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::microseconds>(ed - st).count();
                process_maintenance_wait_time += duration;
            }
        }

        void BatchUpdateHash(std::set<KeyType> *dirty_keys = nullptr) {
            if (dirty_keys == nullptr) {
                std::cout << "[IndexBTree][BatchUpdateHash] dirty_keys == nullptr" << std::endl;
                throw "";
            }
            tree->lazyBatchUpdate(*dirty_keys);
            for (auto it = dirty_keys->begin(); it != dirty_keys->end(); ++it) {
                KeyType key = *it;
                dirty_hash_keys[key] = false;
                dirty_set.erase(key);
            }
        }

    protected:
        void prove(KeyType &key) {

            std::vector<uint8_t> bytes;
            Hash root = tree->getRootHash();

            auto proof = tree->prove(tree->root_, key);

        }

        void pointUpdateHash(KeyType &key) {
            std::unique_lock<std::shared_mutex> lock(ads_mtx);

            std::unique_lock<std::shared_mutex> ads_item_lock(ads_item_mtx[key]);

            tree->lazyPointUpdate(key, false);
            ++hash_update_cnt_;
            pkstats::mark_cleaned(key);

        }

        void batchUpdateHash() {

        }
    };
}

#endif
