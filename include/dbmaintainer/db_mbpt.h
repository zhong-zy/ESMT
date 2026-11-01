
#ifndef INDEX_MAINTAIN_DB_MBPT_H
#define INDEX_MAINTAIN_DB_MBPT_H

#include <utility>
#include <cstdlib>

#include <atomic>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <vector>
#include "kvdb.h"
#include "dbmaintainer/mbpt/mbpt.h"
#include "config.h"
#include "workloadml/workloadml.h"
#include "dbmaintainer/indices/common/order_items.h"
#include "cpputils/parallel_order.h"
#include "cpputils/parallel_order_limited.h"
#include "cpputils/heap_limited.h"
#include "cpputils/timer.h"
#include "workloadml/new_probability_textbook.h"
#include "workloadml/new_distance_textbook.h"
#include "workloadml/new_distance.h"
#include "workloadml/new_distance_delta0.h"

#include "dbmaintainer/indices/multi_index.h"

#include "dbmaintainer/indices/IndexMPT.h"

#include "dbmaintainer/indices/IndexBucketTree.h"
#include "dbmaintainer/indices/IndexBTree.h"
#include "dbmaintainer/indices/IndexLETUS.h"
#include "dbmaintainer/indices/IndexEmpty.h"

#include "SmallSet.h"

extern double read_p;
extern double update_p;
extern double insert_p;
extern int window_size;
extern int64_t rw_offset;

namespace workloadsim {
    using namespace workloadml;
    enum ADS_MOD {
        B1_UpdateImmediate,
        B2_PureLazyUpdate,
        B3_BatchUpdate,
        B4_FrequencyBasedUpdate,
        P1_OfflineTrain,
        B5_UpdateReadsBeforeWrites,
        T1_perfect,
        P2_BinaryModel,
        P3_FreqScoreLearnValid,
        P4_new_base,
        T2_perfect_order,
    };

    enum NEW_MODE {
        probability_textbook=1,
        distance_textbook,
        distance,
        distance_delta0,
    };

    extern int pure_batch_size;
    extern int predict_update_batch_size;
    extern int predict_batch_size;

    class DB_MBPT: public KVDB {
    public:
        uint64_t pre_analyze_time = 0;
        int pre_analyze_cnt = 0;
        ADS_MOD update_mode;

        shared_ptr<Index> index_;
        mutex committer_mtx;
        uint64_t commit_cnt=0;
        uint64_t last_receive_cnt = 0;

#ifdef VERBOSE_MISS
        set<KeyType> predict_keys_1;
#endif
        uint64_t dirty_miss_1 = 0, dirty_override_1 = 0;

        map<KeyType, uint64_t> keys_count;
        set<KeyType> hot_keys;
        set<KeyType> update_keys;
        mutex predict_mtx;
        uint64_t current_ts=0;
        uint64_t latency_sum = 0;
        uint64_t latency_cnt = 0;

        explicit DB_MBPT(const std::string& path, const ADS_MOD update_mode=B1_UpdateImmediate): update_mode(update_mode) {

        }

        void set_update_mode(ADS_MOD update_mode) {
            this->update_mode = update_mode;
        }

    };

    class DB_MBPT_DAII: public DB_MBPT {
    public:
        uint64_t send_cnt = 0;
    private:
        cpputils::Processor<workloads::OpMeta> data_pc, read_data_pc;

        std::thread data_thread;

        std::vector<std::unique_ptr<cpputils::Processor<workloads::OpMeta>>> data_pc_s;
        std::vector<std::thread> data_shard_threads;
        std::mutex stats_mtx;
        std::atomic<uint64_t> data_seen{0};
        std::atomic<bool> measurement_started{false};
        std::mutex meas_mtx;
        std::condition_variable meas_cv;

        inline bool data_sharded() const { return config::data_shard && update_mode != B3_BatchUpdate; }

        inline void data_route_produce(workloads::OpMeta &op) {
            if (data_sharded()) {
                auto mi = std::dynamic_pointer_cast<indices::MultiIndexes>(index_);
                data_pc_s[mi ? mi->route(op.key) : 0]->produce(op);
            } else {
                data_pc.produce(op);
            }
        }

        inline void data_worker(int sid) {
            auto &pc = *data_pc_s[sid];
            uint64_t worker_cnt = 0;
            while (config::process) {
                if (!pc.need_consume()) break;
                auto&& op = pc.consume();
                if (pc.consumed_stop_msg()) break;
                auto seen = data_seen.fetch_add(1) + 1;
                if (seen == (uint64_t)config::init_training_cnt) {
                    if (update_mode == P1_OfflineTrain || update_mode == P3_FreqScoreLearnValid || update_mode == P2_BinaryModel || (update_mode == P4_new_base && config::new_mode == NEW_MODE::distance)) {
                        cout << "process " << seen << " datas (shard " << sid << "), reads: " << read_cnt << endl;
                        start_init_training();
                        unique_lock<mutex> init_training_lock(init_training_mtx);
                        init_training_cond.wait(init_training_lock, [&]{return init_train_finished;});
                        prepare_train_data = false;
                        init_training_lock.unlock();
                    } else {
                        start_init_training();
                        if (req_score_placebo != nullptr) {
                            cout << "start training placebo model" << endl;
                            req_score_placebo->train_once();
                        }
                        finish_init_training();
                    }
#ifdef RUN_AFTER_SCORE
                    {
                        std::unique_lock<std::mutex> lock(score_finish_mtx);
                        score_finish_cond.wait(lock, [&]{ return score_thread_finished; });
                        init_monitor();
                    }
#endif
                    { std::lock_guard<std::mutex> lg(meas_mtx); measurement_started.store(true); }
                    meas_cv.notify_all();
                } else if (seen > (uint64_t)config::init_training_cnt && !measurement_started.load()) {
                    std::unique_lock<std::mutex> lk(meas_mtx);
                    meas_cv.wait(lk, [&]{ return measurement_started.load(); });
                }
                auto request_process_st = steady_clock::now();
                if (op.op == workloads::READ) {
                    auto ret = read(op);
                } else {
                    update(op);
                }
                auto ed = steady_clock::now();
                if (init_train_finished) {
                    std::unique_lock<std::mutex> g(stats_mtx);
                    latency_sum += duration_cast<nanoseconds>(ed - request_process_st).count();
                    op.pi_latency_ed = ed;
                    auto duration = duration_cast<microseconds>(op.pi_latency_ed-op.pi_latency_st).count();
                    pi_latency_list.push_back(duration);
                    if (op.op == workloads::READ) {
                        read_svc_latency_list.push_back(duration_cast<microseconds>(ed - request_process_st).count());
                    }
                    ++latency_cnt;
                    auto request_process_duration = duration_cast<microseconds>(ed - request_process_st).count();
                    request_process_time += request_process_duration;
                }
                ++worker_cnt;
            }
            cout << "data shard worker " << sid << " finished" << endl;
        }
        condition_variable wait_ads_update_cond;

        std::mutex wait_ads_update_mtx;
        uint64_t hash_update_cnt = 0, hash_update_cnt_last = 0;
        int64_t data_wait_time = 0, ads_wait_time = 0, ads_2_wait_time = 0;
        int hit_read_cnt = 0, miss_read_cnt = 0;
        int cnt16 = 0, cnt15 = 0, cnt25 = 0, cnt11 = 0;
        uint64_t receive_cnt = 0;

        std::thread req_buf_thread;
        cpputils::Processor<workloads::OpMeta> req_buf_pc;
        shared_ptr<ReqScore> req_score;

        shared_ptr<ReqScoreDistance> req_score_placebo;

        std::thread score_thread;
        cpputils::Processor<workloads::OpMeta> score_pc;

        int predict_batch2 = predict_batch_size;

        std::thread training_thread;
        int init_training_cnt = config::init_training_cnt;
        int predict_batch = predict_batch_size;

        std::mutex init_training_mtx;
        condition_variable init_training_cond;
        bool init_train_start = false, init_train_finished = false;
        bool prepare_train_data = false;

        cpputils::Processor<OperLogItem> operator_pc;
        std::thread operator_thread;

        uint64_t feature_prepare_time = 0;
        uint64_t model_inference_time = 0;
        uint64_t request_process_time = 0;

        uint64_t wait_maintenance_time2 = 0;

        uint64_t kv_time_us = 0;
        uint64_t prove_total_us = 0;

#ifdef OLD_MODE
        WorkloadMonitor wm;
        WM2 wm2;
#endif
#ifdef TEXTBOOK_DEBUG
        mutex io_mtx;
#endif
#ifdef RUN_AFTER_SCORE
        std::mutex score_finish_mtx;
        std::condition_variable score_finish_cond;
        bool score_thread_finished = false;
#endif
        vector<long> pi_latency_list;

        vector<long> read_svc_latency_list;

    public:
        DB_MBPT_DAII(const std::string& path, const ADS_MOD update_mode=B1_UpdateImmediate): DB_MBPT(path, update_mode)
#ifdef OLD_MODE
                                                                                             , wm(workloads::init_record_count, init_training_cnt, predict_batch, config::wr_both)
                                                                                             , wm2(workloads::init_record_count, init_training_cnt, predict_batch)
#endif
                                                                                             {
            ADSMaintainMode monitor_mode = Frequency;
            if (update_mode == B4_FrequencyBasedUpdate) {
                monitor_mode = Frequency;

            } else if (update_mode == P1_OfflineTrain || update_mode == P3_FreqScoreLearnValid) {
                monitor_mode = Boost;

                prepare_train_data = true;
            }
#ifdef OLD_MODE
            wm.set_mode(monitor_mode);
#endif
            data_pc.name = "data_pc";
            read_data_pc.name = "read_data_pc";
            score_pc.name = "score_pc";
            operator_pc.name = "operator_pc";

            pkstats::ensure((size_t) workloads::init_record_count + 1);
            pkstats::ensure_pk((size_t) workloads::init_record_count + 1);

            data_pc.msgs_buffer_size = config::total_ops;

            {
                int dS = (config::data_shard ? (config::shard_cnt > 0 ? config::shard_cnt : 1) : 1);
                for (int i = 0; i < dS; ++i) {
                    data_pc_s.emplace_back(new cpputils::Processor<workloads::OpMeta>());
                    data_pc_s.back()->name = "data_pc_s" + std::to_string(i);
                    data_pc_s.back()->msgs_buffer_size = config::total_ops;
                }
            }
            operator_pc.msgs_buffer_size = config::total_ops;

            req_buf_pc.name = "req_buf_pc";
            req_buf_pc.msgs_buffer_size = 1;
            {
                const char *sb = std::getenv("ESMT_SCORE_BUF");
                int64_t n = (sb != nullptr) ? std::atoll(sb) : config::score_buf;
                score_pc.msgs_buffer_size = (n > 0) ? n : config::total_ops;
            }
#ifdef ADS_QUEUE_LIMITED
            ads_pc.set_limit_height(config::ads_queue_limit_height);
#endif
        }

        uint64_t op_offset_seen_ = 0;
        void submit_op(workloads::OpMeta& op) override {

            if (op_offset_seen_ < (uint64_t) config::op_offset) { ++op_offset_seen_; return; }

            req_buf_pc.produce(op);
        }

        void process_always() override;

        void process_join() override {
            req_buf_pc.finish_produce();
            req_buf_thread.join();
            cout << "req_buf_thread.join();" << endl;

            score_pc.finish_produce();
            score_thread.join();
            cout << "score_thread.join();" << endl;

            if (config::data_shard) {
                for (auto &pc : data_pc_s) pc->finish_produce();
                for (auto &t : data_shard_threads) t.join();
                cout << "data_shard_threads joined: " << data_shard_threads.size() << endl;
            } else {
            data_pc.finish_produce();
            data_thread.join();
            cout << "data_thread.join();" << endl;
            }
            data_finished_time = steady_clock::now();
            read_data_pc.finish_produce();

            index_->join();
            cout << "ads_thread.join();" << endl;

            if (config::operator_enable) {
            operator_pc.finish_produce();
            operator_thread.join();
            }

            training_thread.join();

            monitor_thread_stop = true;
            monitor_thread.join();
        }

        DBResult read(workloads::OpMeta& op) override;

        void update(workloads::OpMeta& op) override;

        void init_monitor() {
            read_cnt = 0;
            write_cnt = 0;
            update_cnt = 0;
            read_cnt_last = 0;
            write_cnt_last = 0;
            update_cnt_last = 0;
            hash_update_cnt = 0;
            hash_update_cnt_last = 0;
            r_dirty_cnt = 0;
            data_wait_time = 0;
            ads_wait_time = 0;
            ads_2_wait_time = 0;

                index_->ClearHash();

            start = steady_clock::now();

            latency_anchor = start;
            latency_op_cnt = 0;

            if (index_ != nullptr) index_->reset_ads_stats();

            if (index_ != nullptr) index_->reset_stats();
            cout << "init monitor" << endl;
        }

        void start_init_training() {
            init_train_start = true;
            init_training_cond.notify_one();
        }

        void finish_init_training() {
            sleep(10);

            cout << "ADS Prepare..." << endl;
            shared_ptr<ADSStore> db, data_db;
            if (config::ADS_store_type == ROCKSDB) {
                db = make_shared<ADSStoreRocksDB>("./db_test_shrubs");
            } else if (config::ADS_store_type == Memory) {
                db = make_shared<ADSStoreMTMemory>();
                data_db = make_shared<ADSStoreRocksDB>("./db_test_data");
            }

            if (config::shard_cnt > 1) {
                index_ = make_shared<indices::ShardPartitionIndex>(db, static_cast<indices::IndexType>(config::index_type));
            } else if (config::index_type == MerkleTree) {
                if (config::shrubs_type == SHRUBS) {
                    index_ = make_shared<indices::Shrubs>(db, MerkleTree);
                } else if (config::shrubs_type == NONE) {
                    index_ = make_shared<indices::IndexMT>(db);
                }
            } else if (config::index_type == MBT) {
                db = nullptr;
                index_ = make_shared<indices::IndexBTree>(db);
            } else if (config::index_type == MPT) {
                db = nullptr;
                index_ = make_shared<indices::IndexMPT>(db);
            } else if (config::index_type == BucketTree) {
                db = nullptr;
                index_ = make_shared<indices::IndexBucketTree>(db);
            } else if (config::index_type == LETUS) {
                db = nullptr;
                index_ = make_shared<indices::IndexLETUS>(db);
            } else if (config::index_type == EMPTY) {
                db = nullptr;
                index_ = make_shared<indices::IndexEmpty>(db);
            }
            if (config::process) {
                if (config::index_type == MerkleTree || config::index_type == MBT || config::index_type == MPT || config::index_type == BucketTree || config::index_type == LETUS || config::index_type == EMPTY) {
                    vector<pair<KeyType, double>> init_data(workloads::init_record_count);

                    for (uint64_t i = 0; i < workloads::init_record_count; ++i) {
                        if (config::new_mode == NEW_MODE::distance && req_score != nullptr) {
                            init_data[i] = make_pair(i, static_pointer_cast<ReqScoreDistance>(req_score)->freq_score(i));
                        } else if (config::new_mode == NEW_MODE::probability_textbook && req_score != nullptr) {
                            init_data[i] = make_pair(i, static_pointer_cast<ReqScoreTextbook>(req_score)->freq_score(i));
                        } else {
                            init_data[i] = make_pair(i, -i);

                        }
                    }
                    sort(init_data.begin(), init_data.end(), [](pair<KeyType, double> &a, pair<KeyType, double> &b) {
                        return a.second > b.second;
                    });
                    cout << "init count: " << workloads::init_record_count << endl;
                    vector<KeyType> init_keys(workloads::init_record_count);
                    for (int i = 0; i < workloads::init_record_count; ++i) {
                        init_keys[i] = init_data[i].first;
                    }
                    index_->build(init_keys.begin(), init_keys.end());
                }
            }
            cout << "ADS Prepare Finished" << endl;

            init_monitor();
            init_train_finished = true;
            init_train_start = false;
            init_training_cond.notify_all();
        }

    };

    inline DBResult DB_MBPT_DAII::read(workloads::OpMeta &op) {
        static ofstream wait_out(log_name.substr(0, log_name.find_last_of('.')) + "_block_read.txt", ios::out);

        if (init_train_finished) pkstats::on_read(op.key, op.ts);

        string ret;
        if (init_train_finished) {

            auto kv_st = steady_clock::now();
            ret = *index_->search(op.key);
            auto kv_ed = steady_clock::now();
            auto kv_us = duration_cast<microseconds>(kv_ed - kv_st).count();
            if (config::operator_enable) operator_pc.produce({op.key, op.op, op.op, cpputils::getTimestamp()});
            auto pv_st = steady_clock::now();
            index_->Prove(op.key, op.ts);
            auto pv_ed = steady_clock::now();
            auto pv_us = duration_cast<microseconds>(pv_ed - pv_st).count();
            if (config::operator_enable) operator_pc.produce({op.key, op.op, READ_HASH, cpputils::getTimestamp()});
            if (config::data_shard) { std::lock_guard<std::mutex> g(stats_mtx); kv_time_us += kv_us; prove_total_us += pv_us; }
            else { kv_time_us += kv_us; prove_total_us += pv_us; }
        }
        if (init_train_finished) pkstats::clear_cleaned(op.key);

        if (config::data_shard) { std::lock_guard<std::mutex> g(stats_mtx); ++read_cnt; } else { ++read_cnt; }
        return {ret};
    }

    inline void DB_MBPT_DAII::update(workloads::OpMeta& op) {
        if (init_train_finished) {

            auto kv_st = steady_clock::now();
            index_->insert(op.key, op.value, false);
            auto kv_ed = steady_clock::now();
            auto kv_us = duration_cast<microseconds>(kv_ed - kv_st).count();
            if (config::data_shard) { std::lock_guard<std::mutex> g(stats_mtx); kv_time_us += kv_us; }
            else kv_time_us += kv_us;

            if (config::operator_enable) operator_pc.produce({op.key, op.op, op.op, cpputils::getTimestamp()});
            bool immediate = update_mode == B1_UpdateImmediate;

            if (pkstats::take_cleaned(op.key)) pkstats::add_wasted();

            int pk_lvl = 2;
            bool pk_skip = false;
            double ph = 0.5;
            if (config::pk_enable) {
                ph = (config::pk_estimator == 2)
                            ? pkstats::phat_lr(op.key, op.ts)
                            : pkstats::phat_freq(op.key, config::pk_alpha);
                if (config::pk_mode == 1)      pk_lvl  = (ph >= config::pk_pstar) ? 2 : 3;
                else if (config::pk_mode == 2) pk_skip = (ph <  config::pk_pstar);
                else if (config::pk_mode == 3) {

                    int L = config::pk_levels > 1 ? config::pk_levels : 8;
                    int cls = (int) (ph * L);
                    if (cls > L - 1) cls = L - 1;
                    if (cls < 0) cls = 0;
                    pk_lvl = 1 + (L - 1 - cls);
                }
                else if (config::pk_mode == 4) {

                    pk_lvl = 2;
                }
            }

            workloadml::Score pk_score = op.priority;
            if (config::pk_enable && config::pk_mode == 4) pk_score = workloadml::Score(ph, op.priority.score1);
            pkstats::on_write(op.key, op.ts);

            if (update_mode == T1_perfect) {
                if (op.next == 1) {
                    if (op.next_read_ts != 0) {
                        index_->PointUpdateHash({op.key, static_cast<double>(-op.next_read_ts), op.op, op.ts, 2}, false);
                    }
                }
            } else if (update_mode == T2_perfect_order) {
                if (op.next_read_ts != 0) {
                    index_->PointUpdateHash({op.key, static_cast<double>(-op.next_read_ts), op.op, op.ts, 2}, false);
                }
            } else if (update_mode != B2_PureLazyUpdate) {
                auto wait_maintenance_st = steady_clock::now();

                static const bool seq_inline = (std::getenv("SEQ_INLINE") != nullptr);
                if (!pk_skip) {
                    if (seq_inline && immediate) index_->InlineUpdateHash({op.key, pk_score, op.op, op.ts, pk_lvl});
                    else index_->PointUpdateHash({op.key, pk_score, op.op, op.ts, pk_lvl}, immediate);
                }
                auto wait_maintenance_ed = steady_clock::now();
                auto wait_maintenance_duration = duration_cast<microseconds>(wait_maintenance_ed-wait_maintenance_st).count();
                if (config::data_shard) { std::lock_guard<std::mutex> g(stats_mtx); wait_maintenance_time2 += wait_maintenance_duration; }
                else wait_maintenance_time2 += wait_maintenance_duration;
            }
        }
        if (config::data_shard) { std::lock_guard<std::mutex> g(stats_mtx); ++update_cnt; } else { ++update_cnt; }
    }
}

#endif
