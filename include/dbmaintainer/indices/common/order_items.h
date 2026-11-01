
#ifndef INDEX_MAINTAIN_ORDER_ITEMS_H
#define INDEX_MAINTAIN_ORDER_ITEMS_H

#include <queue>
#include <iostream>
#include "workloads/ycsb_workload.h"
#include <set>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include <cstdlib>
#include "cpputils/parallel_order.h"

namespace indices {
    class OperLogItem {
    public:
        KeyType key;
        workloads::Operation call_from, op;
        int64_t timestamp;
    };

    class TimeOrderItem {
    public:
        KeyType key;
        workloads::Operation op;
        uint64_t time = 0;

        bool operator>(TimeOrderItem &b) const {

            if (this->op == b.op) {
                return this->time > b.time;
            } else {
                return this->op > b.op;
            }
        }
    };

#include "workloadml/score.h"
    class FreqOrderItem {
    public:
        KeyType key;

        workloadml::Score freq_score = 0;
        workloads::Operation op;
        int64_t time = 0;
        int level = 0;
        double valid_time=0;

        bool operator<(FreqOrderItem& b) const {

            if (this->level == b.level) {
                if (this->freq_score == b.freq_score) {
                    return this->time > b.time;
                } else {
                    return this->freq_score < b.freq_score;
                }
            } else {
                return this->level > b.level;
            }
        }

        bool operator>(FreqOrderItem& b) const {

            if (this->level == b.level) {
                if (this->freq_score == b.freq_score) {
                    return this->time < b.time;
                } else {
                    return this->freq_score > b.freq_score;
                }
            } else {
                return this->level < b.level;
            }
        }
        bool operator>=(FreqOrderItem& b) const {

            if (*this < b) {
                return false;
            } else {
                return true;
            }
        }

    };

    template<class T>
    class Less {
    public:
        bool operator()(T &a, T &b) const {
            return a < b;
        }
    };

    template<class T>
    class Greater {
    public:
        bool operator()(T &a, T &b) const {
            return a > b;
        }
    };

#ifndef MMD_ENABLE_DEDUP
#define MMD_ENABLE_DEDUP 0
#endif

#if MMD_ENABLE_DEDUP

    struct PendingLess {
        bool operator()(const FreqOrderItem &a, const FreqOrderItem &b) const {
            FreqOrderItem x = a, y = b;
            return y < x;
        }
    };

    class AdsQueue {
    public:
        std::string name;
        int msgs_buffer_size = 500000;
        struct Ent { std::set<FreqOrderItem, PendingLess>::iterator it; int64_t time; };
        uint64_t dedup_merge_cnt = 0, dedup_update_cnt = 0, dedup_promote_cnt = 0;
        uint64_t queue_len_max = 0, queue_len_sum = 0, queue_len_samples = 0;

        void produce(FreqOrderItem op) {
            if (!dedup_on()) {
                base.msgs_buffer_size = msgs_buffer_size;
                base.produce(std::move(op));
                track_len(base.size());
                return;
            }
            std::unique_lock<std::mutex> lk(mtx);
            auto it = idx.find(op.key);
            if (it == idx.end()) {
                auto nit = pending.insert(op).first;
                idx.emplace(op.key, Ent{nit, op.time});
                if (pending.size() > queue_len_max) queue_len_max = pending.size();
            } else {
                Ent &e = it->second;
                if (op.time > e.time) e.time = op.time;
                const bool is_read = (op.level == 0);
                if (is_read || rank_earlier(op, *e.it)) {
                    FreqOrderItem repl = op;
                    repl.time = e.time;
                    pending.erase(e.it);
                    e.it = pending.insert(repl).first;
                    if (is_read) ++dedup_promote_cnt; else ++dedup_update_cnt;
                } else {
                    ++dedup_merge_cnt;
                }
            }
            cv.notify_one();
        }

        bool need_consume() {
            if (!dedup_on()) return base.need_consume();
            std::lock_guard<std::mutex> lk(mtx);
            if (finished && pending.empty()) { stop_msg = true; return false; }
            return true;
        }

        FreqOrderItem consume() {
            if (!dedup_on()) {
                FreqOrderItem m = base.consume();
                const size_t n = base.size();
                track_len(n);
                queue_len_sum += n; ++queue_len_samples;
                return m;
            }
            std::unique_lock<std::mutex> lk(mtx);
            cv.wait(lk, [&] { return !pending.empty() || finished; });
            if (pending.empty()) { stop_msg = true; return FreqOrderItem(); }
            FreqOrderItem op = *pending.begin();
            auto it = idx.find(op.key);
            if (it != idx.end()) { op.time = it->second.time; idx.erase(it); }
            pending.erase(pending.begin());
            queue_len_sum += pending.size(); ++queue_len_samples;
            return op;
        }

        void reset_len_stats() { queue_len_max = 0; queue_len_sum = 0; queue_len_samples = 0; }

        bool consumed_stop_msg() { return dedup_on() ? stop_msg : base.consumed_stop_msg(); }
        void finish_produce() {
            if (!dedup_on()) { base.finish_produce(); return; }
            { std::lock_guard<std::mutex> lk(mtx); finished = true; }
            cv.notify_all();
        }
        void stop() { if (!dedup_on()) base.stop(); else stop_msg = true; }
        size_t size() {
            if (!dedup_on()) return base.size();
            std::lock_guard<std::mutex> lk(mtx);
            return pending.size();
        }
        uint64_t len_max() const { return queue_len_max; }
        uint64_t len_avg() const { return queue_len_samples == 0 ? 0 : queue_len_sum / queue_len_samples; }

    private:
        void track_len(size_t n) {
            if (n > queue_len_max) queue_len_max = n;
        }
        static bool dedup_on() { static const bool v = (std::getenv("ADS_DEDUP") != nullptr); return v; }
        static bool rank_earlier(const FreqOrderItem &a, const FreqOrderItem &b) {
            FreqOrderItem x = a, y = b;
            return y < x;
        }
        cpputils::OrderProcessor<FreqOrderItem,
            std::priority_queue<FreqOrderItem, std::vector<FreqOrderItem>, Less<FreqOrderItem>>> base;
        std::set<FreqOrderItem, PendingLess> pending;
        std::unordered_map<KeyType, Ent> idx;
        mutable std::mutex mtx;
        std::condition_variable cv;
        bool finished = false, stop_msg = false;
    };

#else

    struct PendingLess {
        bool operator()(const FreqOrderItem &a, const FreqOrderItem &b) const {
            FreqOrderItem x = a, y = b;
            return y < x;
        }
    };

    class AdsQueue : public cpputils::OrderProcessor<FreqOrderItem,
            std::priority_queue<FreqOrderItem, std::vector<FreqOrderItem>, Less<FreqOrderItem>>> {
    public:

        uint64_t len_max() const { return 0; }
        uint64_t len_avg() const { return 0; }
        void reset_len_stats() {}
        uint64_t dedup_merge_cnt = 0, dedup_update_cnt = 0, dedup_promote_cnt = 0;
    };

#endif

}

#endif
