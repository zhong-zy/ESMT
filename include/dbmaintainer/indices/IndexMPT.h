
#ifndef SHRUBS_LIKE_INDEX_INDEXMPT_H
#define SHRUBS_LIKE_INDEX_INDEXMPT_H

#include <shared_mutex>
#include <thread>
#include "dbmaintainer/indices/index.h"
#include "dbmaintainer/indices/common/order_items.h"
#include "cpputils/sync_table_flags.h"
#include "dbmaintainer/mpt/mpt.h"
#include <chrono>

#define SINGLE_TREAD

namespace indices {
    class IndexMPT: public Index {
        std::shared_ptr<MerklePatriciaTrie> tree;
        uint64_t tree_id_ = 0;

        std::unordered_map<KeyType, std::shared_mutex> data_mtx;
        std::unordered_map<KeyType, std::shared_mutex> ads_item_mtx;
        std::shared_mutex ads_mtx;

        double max_score = 1;
        std::shared_ptr<std::thread> ads_thread;
        AdsQueue ads_pc;

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
        uint64_t read_wait = 0;
        uint64_t process_maintenance_wait_time = 0;
        uint64_t hit_cnt = 0;
        uint64_t loss_cnt = 0;

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
        }

    public:
        IndexMPT(std::shared_ptr<ADSStore> &db, uint64_t tree_id=0): tree_id_(tree_id) {

            tree = std::make_shared<MerklePatriciaTrie>();
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

                    if (op.time <= last_ts) {
                        continue;
                    }

                    last_update_ts[op.key] = op.time;

                    if (dirty_hash_keys.get(op.key)) {
                        pointUpdateHash(op.key);
                        dirty_hash_keys[op.key] = false;
                        dirty_set.erase(op.key);
                        hit_cnt += (op.op == UPDATE);
                        loss_cnt += (op.op == READ);
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
            auto skey = std::to_string(key);
            tree->insert(skey, const_cast<std::string &>(value), false);
            dirty_hash_keys[key] = true;
            dirty_set.insert(key);

        }

        std::shared_ptr<std::string> search(KeyType &key) final {
#ifndef SINGLE_TREAD
            std::shared_lock<std::shared_mutex> lock(data_mtx[key]);
#endif
            auto ret = std::make_shared<std::string>();

            auto skey = std::to_string(key);
            auto [ret1, found] = tree->Get(skey);
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
                    auto st = std::chrono::steady_clock::now();
                    PointUpdateHash({key, max_score, READ, ts, 0}, true);
                    auto ed = std::chrono::steady_clock::now();
                    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(ed - st).count();
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
                std::cout << "[IndexBTree][batchUpdateHash] dirty_keys == nullptr" << std::endl;
                throw "";
            }
            std::set<std::string> dirty_skeys;
            for (auto it = dirty_keys->begin(); it != dirty_keys->end(); ++it) {
                dirty_skeys.insert(std::to_string(*it));
            }
            tree->lazyBatchUpdate(dirty_skeys);
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
            auto skey = std::to_string(key);
            auto proof = tree->prove(skey);

        }

        void pointUpdateHash(KeyType &key) {
            std::unique_lock<std::shared_mutex> lock(ads_mtx);

            std::unique_lock<std::shared_mutex> ads_item_lock(ads_item_mtx[key]);
            auto skey = std::to_string(key);
            tree->lazyPointUpdate(skey, false);
            ++hash_update_cnt_;

        }

        void batchUpdateHash() {

        }
    };
}

#endif
