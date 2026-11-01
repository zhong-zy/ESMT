
#include "dbmaintainer/db_mbpt.h"
#include "dbmaintainer/indices/common/order_items.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include "cpputils/size.h"
#include <iostream>

namespace pkstats {
    std::vector<char> g_cleaned;
    std::atomic<uint64_t> g_wasted(0);
    std::atomic<uint64_t> g_marked(0);
    std::vector<uint32_t> g_nr, g_nw;
    std::vector<int64_t> g_ts_any, g_ts_read;

    std::vector<int64_t> g_ts_write;
    std::atomic<uint64_t> g_bg_cnt(0), g_bg_gcnt(0), g_bg_gsum(0), g_bg_le3(0), g_bg_le15(0), g_bg_le63(0), g_bg_q_sum(0);
}

extern string log_name;
extern string exp_results_name;

namespace workloadsim {
    int pure_batch_size = 100;
    int predict_update_batch_size = 100;
    int predict_batch_size = 1;

    int *dirty;
    int *last_cf, *last_op;

    void DB_MBPT_DAII::process_always() {

        dirty = new int[workloads::init_record_count];
        last_cf = new int[workloads::init_record_count];
        last_op = new int[workloads::init_record_count];
        pi_latency_list.reserve(config::process_cnt);

        start = steady_clock::now();

        monitor_thread = thread{[&]{
#ifdef PRINT_LOG
            ofstream of_run(log_name);
#endif
            if (config::exp_log_name.length() > 0) {
                exp_results_name = config::exp_log_name;
            }
            ifstream of_exp_test(exp_results_name);
            stringstream ss, ss_t;
            bool need_title = false;

            if (!of_exp_test.good() || of_exp_test.peek() == std::ifstream::traits_type::eof()) {

                of_exp_test.close();
                need_title = true;
            }
            ofstream of_exp(exp_results_name, ios::app);
#ifdef PRINT_LOG
            of_run << "run time(s)\top count\tTPS\tTPS(curr)\tLatency AVG(μs)\tpi_latency99(μs)\tdata thread wait(ms)\thash count\thit hash cnt\tloss hash cnt\tTPS\tTPS(curr)\tads queue size\tads thread1 wait(ms)\tads predict queue size\tads thread2 wait(ms)\tread count\tblocked reads\tdelta blocked reads\tread wait(ms)\tMaintain wait\tTPS\tTPS(curr)\tupdate count\tTPS\tTPS(curr)" << endl;
#endif
            uint64_t total_read, total_update, total_insert, read_tps, update_tps, total_duration, read_duration, write_duration, ops, ops_tps;
            uint64_t hash_update_duration, total_hash_update, hash_tps;

            uint64_t data_wait = 0, ads_wait = 0, ads_wait2 = 0;
            size_t ads_pc_size = 0, ads_predict_pc_size = 0;
            uint64_t last_blocked_reads = 0, r_dirtys_delta=0;
            uint64_t total_batch_loss = 0, last_batch_loss = 0;

            int error_thread = 50;
            int error_cnt = 0;
            while (!monitor_thread_stop) {

                    if (init_train_start) {
                        unique_lock<mutex> init_training_lock(init_training_mtx);
                        init_training_cond.wait(init_training_lock, [&]{return init_train_finished;});
                        init_training_lock.unlock();
                    }

                this_thread::sleep_for(verbose_interval);
                auto t = steady_clock::now();
                auto duration = duration_cast<seconds>(t - start).count();

                total_read = read_cnt;
                total_insert = write_cnt;
                total_update = update_cnt;

                total_batch_loss = batch_loss_cnt;
                if (index_ != nullptr) {
                    total_hash_update = index_->hash_update_cnt();
                } else {
                    total_hash_update = 0;
                }

#ifdef RUN_AFTER_SCORE
                if (score_thread_finished && !data_pc.consumed_stop_msg() && duration) {
#else
                if (!data_pc.consumed_stop_msg()) {
#endif
                    read_duration = duration;
                    read_tps = total_read / read_duration;

                    write_duration = duration;
                    update_tps = total_update / write_duration;

                    hash_update_duration = duration;
                    hash_tps = total_hash_update / hash_update_duration;
                }
                auto r_dirtys = (index_ != nullptr ? index_->get_blocked_read() : 0);
                auto read_wait = (index_ != nullptr ? index_->get_read_wait() : 0);
                auto process_maintenance_wait_time = (index_ != nullptr ? index_->get_process_maintenance_wait_time() : 0);
                auto hit_hash_cnt = (index_ != nullptr ? index_->get_hit_cnt() : 0);
                auto loss_hash_cnt = (index_ != nullptr ? index_->get_loss_cnt() : 0);
                r_dirtys_delta = r_dirtys - last_blocked_reads;
                last_blocked_reads = r_dirtys;

                ops = read_cnt+write_cnt+update_cnt;
                total_duration = duration;
                ops_tps = (duration ? ops / total_duration : 0);

                auto current_read_tps = total_read - read_cnt_last;
                auto current_update_tps = total_update - update_cnt_last;
                auto current_hash_update_tps = total_hash_update - hash_update_cnt_last;
                auto current_batch_loss = total_batch_loss - last_batch_loss;
                data_wait  = data_wait_time;
                ads_wait = ads_wait_time;

                ads_pc_size = (index_ != nullptr ? index_->ads_queue_size() : 0);
                auto rec_cnt = receive_cnt;
                auto curr_rec_cnt = rec_cnt - last_receive_cnt;
                auto latency_avg = latency_sum / static_cast<double>(latency_cnt*1000);

                auto pi_latency_99_pos = long(ops * 0.9);
                auto pi_latency_99 = (init_train_finished) ? pi_latency_list[pi_latency_99_pos] : 0;

                if (current_read_tps + current_update_tps + curr_rec_cnt == 0) {
                    ++error_cnt;
                    if (error_cnt == error_thread) {
                        cout << "killed" << endl;
                        exit(-1);
                    }
                } else {
                    error_cnt = 0;
                }
#ifdef PRINT_LOG
                of_run << duration << "\t";
                of_run << ops << "\t";
                of_run << ops_tps << "\t";
                of_run << current_read_tps + current_update_tps << "\t";
                of_run << latency_avg << "\t";
                of_run << pi_latency_99 << "\t";
                of_run << data_wait << "\t";
                of_run << total_hash_update << "\t";
                of_run << hit_hash_cnt << "\t";
                of_run << loss_hash_cnt << "\t";
                of_run << hash_tps << "\t";
                of_run << current_hash_update_tps << "\t";
                of_run << ads_pc_size << "\t";
                of_run << ads_wait << "\t";
                of_run << ads_predict_pc_size << "\t";
                of_run << ads_wait2 << "\t";
                of_run << total_read << "\t";
                of_run << r_dirtys << "\t";
                of_run << r_dirtys_delta << "\t";
                of_run << read_wait << "\t";
                of_run << process_maintenance_wait_time << "\t";
                of_run << read_tps << "\t";
                of_run << current_read_tps << "\t";
                of_run << total_update << "\t";
                of_run << update_tps << "\t";
                of_run << current_update_tps << "\t";
                of_run << endl;
#endif
                printf("run time: %ld, received cnt: %lu, curr receive cnt: %lu, total ops: %lu, TPS: %lu, TPS(curr): %lu, latency avg(μs): %.2f, pi_latency99(μs): %ld, data thread wait(ms): %ld, batch loss: %ld, total hashes: %lu, hit hash cnt: %lu, loss hash cnt: %lu, TPS: %lu, TPS(curr): %lu, ads queue size: %ld, ads thread1 wait(ms): %ld, ads predict queue size: %ld, ads thread2 wait(ms): %ld, total reads: %lu, blocked reads: %lu, delta blocked reads: %lu, read wait(ms): %lu, process thread maintain wait: : %lu, TPS: %lu, TPS(curr): %lu, total updates: %lu, TPS: %lu, TPS(curr): %lu, FeatureTime: %lu, InferTime: %lu, ReqTime: %lu, MaintainTime: %lu\n",
                       total_duration, rec_cnt, curr_rec_cnt, ops, ops_tps, current_read_tps + current_update_tps, latency_avg, pi_latency_99, data_wait, current_batch_loss, total_hash_update, hit_hash_cnt, loss_hash_cnt, hash_tps, current_hash_update_tps, ads_pc_size, ads_wait, ads_predict_pc_size, ads_wait2, total_read, r_dirtys, r_dirtys_delta, read_wait, process_maintenance_wait_time, read_tps, current_read_tps, total_update, update_tps, current_update_tps, feature_prepare_time, model_inference_time, request_process_time, read_wait+wait_maintenance_time2);
#ifdef VERBOSE_MISS
                printf("READ: dirty miss 1: %lu, dirty override 1: %lu\n", dirty_miss_1, dirty_override_1);
#endif

                auto dirty_size = (index_ == nullptr ? 0 : index_->dirty_size());

                cout << "dirtyKeys.size() (reference, not synchronized): " << dirty_size << endl;
                cout << "update_keys.size() (reference, not synchronized): " << update_keys.size() << endl;
                read_cnt_last = total_read;
                write_cnt_last = total_insert;
                update_cnt_last = total_update;
                hash_update_cnt_last = total_hash_update;
                last_receive_cnt = rec_cnt;
                last_batch_loss = total_batch_loss;
            }

            auto cnt = hit_read_cnt + miss_read_cnt;
            auto out_cnt2 = cnt25 - hit_read_cnt;
            auto out_cnt1 = cnt15 - miss_read_cnt;
            auto hit_rate = cnt ? static_cast<double>(hit_read_cnt) / cnt : 0.0;
            auto hash_cnt = cnt15+cnt25;
            auto cnt_rate = static_cast<double>(cnt)/hash_cnt;

            auto commit_duration = duration_cast<milliseconds>(finish_commit - commit_st).count();
            cout << "[commit duration] " << commit_duration << " ms" << endl;

            auto commit_tps = receive_cnt * 1000.0 / commit_duration;
            double process_time = duration_cast<milliseconds>(data_finished_time - start).count() / 1000.0;
            double process_tps = ops / process_time;
            std::sort(pi_latency_list.begin(), pi_latency_list.end());

            std::sort(read_svc_latency_list.begin(), read_svc_latency_list.end());
            auto rs_n = static_cast<long>(read_svc_latency_list.size());
            long read_svc_p50 = (rs_n > 0) ? read_svc_latency_list[static_cast<long>(rs_n * 0.50)] : 0;
            long read_svc_p90 = (rs_n > 0) ? read_svc_latency_list[static_cast<long>(rs_n * 0.90)] : 0;
            long read_svc_p99 = (rs_n > 0) ? read_svc_latency_list[static_cast<long>(rs_n * 0.99)] : 0;

            std::string maintainer_method_name = config::maintainer_name[update_mode];
            if (config::maintainer_ids[update_mode] == 9) {
                static const char *new_base_name[] = {"ESMT", "ESMT (Frequency)", "ESMT (Distance)", "ESMT (Learning)"};
                maintainer_method_name = new_base_name[config::new_mode];
            }

            ss_t << "maintainer_id\tmaintainer_method\tmaintainer_id2\tADS\tlearning_score_option\t"
                    "Batch Size\tSliding Window Size\tScale\tread(%)\t"
                    "run time(s)\top count\tThroughput\t"
                    "Unprovable Requests\tRead-triggered Updates\tread count\t"
                    "dataset\tresult_id";
            ss << config::maintainer_ids[update_mode] << "\t"
               << maintainer_method_name << "\t"
               << config::new_mode << "\t"
               << config::index_type << "\t"
               << config::learning_score_option << "\t"
               << config::ads_update_batch << "\t"
               << config::score_window_size << "\t"
               << workloads::init_record_count << "\t"
               << static_cast<int>(read_p * 100) << "\t"
               << std::fixed << std::setprecision(2) << process_time << "\t"
               << ops << "\t" << process_tps << "\t"
               << batch_loss_cnt << "\t"
               << (index_ != nullptr ? index_->get_loss_cnt() : 0) << "\t"
               << total_read << "\t"
               << config::workload_file_name << "\t"
               << config::result_id;

            if (config::out_sched_tput) {
                ss_t << "\tSchedule Generation Throughput";
                ss << "\t" << std::fixed << std::setprecision(2) << commit_tps;
            }

            auto pi_latency_p90 = (pi_latency_list.empty() ? 0 : pi_latency_list[static_cast<long>(pi_latency_list.size() * 0.9)]);
            auto pi_latency_p99 = (pi_latency_list.empty() ? 0 : pi_latency_list[static_cast<long>(pi_latency_list.size() * 0.99)]);
            if (config::out_latency) {
                ss_t << "\tLatency AVG(us)\tLatency p90(us)\tLatency p99(us)";
                ss << "\t" << latency_sum / static_cast<double>(latency_cnt * 1000)
                   << "\t" << pi_latency_p90 << "\t" << pi_latency_p99;
            }

            auto read_wait = (index_ != nullptr ? index_->get_read_wait() : 0);
            auto process_maintenance_wait_time = (index_ != nullptr ? index_->get_process_maintenance_wait_time() : 0);
            if (config::out_breakdown) {
                double n_ops = (ops > 0 ? static_cast<double>(ops) : 1.0);
                double req_proc = (static_cast<double>(kv_time_us) + static_cast<double>(prove_total_us)
                                   - static_cast<double>(read_wait)) / n_ops;
                double sched_gen = (static_cast<double>(feature_prepare_time) + static_cast<double>(model_inference_time)) / n_ops;
                double mpu = static_cast<double>(read_wait + wait_maintenance_time2) / n_ops;
                ss_t << "\tRequest Process(us/op)\tSchedule Generation(us/op)\tMerkle Path Update(us/op)\tTotal(us/op)";
                ss << "\t" << req_proc << "\t" << sched_gen << "\t" << mpu
                   << "\t" << (req_proc + sched_gen + mpu);
            }

            ss_t << "\n";
            ss << "\n";

            if (need_title) {
                of_exp << ss_t.str() << flush;
            }
#ifdef PRINT_LOG
            of_run << ss_t.str() << flush;
            of_run << ss.str() << flush;
            of_run.close();
#endif

            of_exp << ss.str() << flush;

                workloadml::g_score_trace.finish();
                if (index_ != nullptr && index_->get_prove_us_cnt() > 0) {
                    std::cout << "[prove_us] sum=" << index_->get_prove_us_sum()
                              << " cnt=" << index_->get_prove_us_cnt()
                              << " mean_us=" << ((double) index_->get_prove_us_sum() / (double) index_->get_prove_us_cnt())
                              << std::endl;
                }
                of_exp.close();
        }};

        req_buf_thread = thread([&] {
            queue<OpMeta> req_buf;
#ifdef TEXTBOOK_DEBUG
            unique_lock<mutex> lock(io_mtx);
            lock.unlock();
#endif
            if (update_mode == P4_new_base) {
                if (config::new_mode == NEW_MODE::probability_textbook) {
                    cout << "new ReqScoreTextbook" << endl;
                    req_score = make_shared<ReqScoreTextbook>(config::score_window_size, workloads::init_record_count);
                } else if (config::new_mode == NEW_MODE::distance_textbook) {
                    cout << "new ReqScoreDistanceTextbook" << endl;
                    req_score = make_shared<ReqScoreDistanceTextbook>(config::score_window_size, workloads::init_record_count);
                } else if (config::new_mode == NEW_MODE::distance) {
                    cout << "new ReqScoreDistance" << endl;
                    auto req_dis_score = make_shared<ReqScoreDistance>(config::score_window_size, workloads::init_record_count);
                    req_dis_score->train_mode();
                    req_score = req_dis_score;
                } else if (config::new_mode == NEW_MODE::distance_delta0) {
                    cout << "new ReqScoreDistanceDelta0" << endl;
                    req_score = make_shared<ReqScoreDistanceDelta0>(config::score_window_size, workloads::init_record_count);
                }

                if (config::placebo_inference && config::new_mode == NEW_MODE::probability_textbook) {
                    auto p = make_shared<ReqScoreDistance>(config::score_window_size, workloads::init_record_count);
                    p->train_mode();
                    req_score_placebo = p;
                    cout << "new ReqScoreDistance (placebo, option=" << config::learning_score_option << ")" << endl;
                }
            }

            int cnt = 0;
            while (config::process) {
                if (!req_buf_pc.need_consume()) {
                    break;
                }
                auto op = req_buf_pc.consume();
                if (req_buf_pc.consumed_stop_msg()) {
                    break;
                }

#ifdef TEXTBOOK_DEBUG
                lock.lock();
                puts(("[req_buf_thread] op ts " + to_string(op.ts)).c_str());
                fflush(stdout);
                lock.unlock();
#endif
                if (update_mode == P4_new_base) {
                    auto feature_prepare_st = steady_clock::now();
                    req_score->push_req(op);
                    if (req_score_placebo != nullptr) req_score_placebo->push_req(op);
                    auto feature_prepare_ed = steady_clock::now();
                    auto feature_prepare_duration = (cnt >= config::init_training_cnt) ? duration_cast<microseconds>(feature_prepare_ed-feature_prepare_st).count() : 0;
                    feature_prepare_time += feature_prepare_duration;
                }
                req_buf.push(op);
                if (req_buf.size() > config::textbook_preread_size) {
                    const auto& out_req = req_buf.front();
                    score_pc.produce(out_req);
                    req_buf.pop();
                }
                ++cnt;

                receive_cnt = (init_train_finished ? receive_cnt + 1 : receive_cnt);

            }

            while (!req_buf.empty()) {
                if (update_mode == P4_new_base && config::new_mode == NEW_MODE::distance_textbook) {
                    dynamic_pointer_cast<ReqScoreDistanceTextbook>(req_score)->push_req_null();
                }
                const auto& out_req = req_buf.front();
                score_pc.produce(out_req);
                req_buf.pop();

            }
        });

        score_thread = thread([&]{
            vector<workloads::OpMeta> ops;
            uint64_t cnt = 0;
#ifdef TEXTBOOK_DEBUG
            unique_lock<mutex> lock(io_mtx);
            lock.unlock();
#endif
            while (cnt < init_training_cnt) {
                if (!score_pc.need_consume()) {
                    break;
                }
                auto&& op = score_pc.consume();
                if (score_pc.consumed_stop_msg()) {
                    break;
                }
                ++cnt;
#ifdef TEXTBOOK_DEBUG
                lock.lock();
                puts(("[score_thread] op ts " + to_string(op.ts)).c_str());
                fflush(stdout);
                lock.unlock();
#endif

#ifdef OLD_MODE
                if (update_mode == P1_OfflineTrain || update_mode == P3_FreqScoreLearnValid) {
                    if (op.op == READ) {
                        wm.update_once(op, true);
                    } else if (op.op == UPDATE) {

                    }
                } else if (update_mode == P2_BinaryModel) {
                    if (op.op == READ) {
                        wm.update_once(op, false);
                        wm2.update_once(op, false);
                    } else if (op.op == UPDATE) {
                        wm2.update_once(op, true);
                    }
                } else if (update_mode == B4_FrequencyBasedUpdate) {
                    if (op.op == READ) {
                        wm.update_once(op, false);
                    }
                }
#endif
                data_route_produce(op);
            }

            unique_lock<mutex> init_training_lock(init_training_mtx);
            init_training_cond.wait(init_training_lock, [&]{return init_train_finished;});
            init_training_lock.unlock();

            if (update_mode == P4_new_base && config::new_mode == NEW_MODE::distance) {
                static_pointer_cast<ReqScoreDistance>(req_score)->valid_mode();
            }
            if (update_mode == P4_new_base && req_score_placebo != nullptr) {
                req_score_placebo->valid_mode();
            }

            vector<OpMeta> op_buf;
            vector<uint64_t> keys_buf;

            vector<Score> scores;
            vector<uint64_t> update_idxes;
            cnt=0;
            uint64_t cnt_batch = 0;
            commit_st = steady_clock::now();
            cout << "[commit st]" << endl;
            auto send_interval = (config::send_ops) ? microseconds(int(1000000.0 / config::send_ops)) : microseconds(0);

            while(config::process) {
                if (!score_pc.need_consume()) {
                    break;
                }
                auto&& op = score_pc.consume();
                if (score_pc.consumed_stop_msg()) {
                    break;
                }
                ++cnt;

#ifdef TEXTBOOK_DEBUG
                lock.lock();
                puts(("[score_thread] op ts " + to_string(op.ts)).c_str());
                fflush(stdout);
                lock.unlock();
#endif

#ifdef OLD_MODE
                if (update_mode == P1_OfflineTrain) {
                    if (op.op == READ) {
                        wm.update_once(op, false);
                    } else {
                        auto meta = wm.req_metas[op.key];
                        if (meta != nullptr) {
                            wm.predict_data.push_back(meta, op.key, op.ts);
                            wm.predict_data.rank();
                            op.valid_time = wm.predict_data.scores[0];

                            op.priority = -wm.predict_data.scores[0];
                            wm.predict_data.next();
                        } else {
                            op.valid_time = 1000000;
                            op.priority = -1000000;
                        }
                    }
                    data_route_produce(op);
                } else if (update_mode == P3_FreqScoreLearnValid) {
                    if (op.op == READ) {
                        wm.update_once(op, false);
                    } else {
                        auto meta = wm.req_metas[op.key];
                        if (meta != nullptr) {
                            wm.predict_data.push_back(meta, op.key, op.ts);
                            wm.predict_data.rank();
                            op.valid_time = wm.predict_data.scores[0];
                            op.priority = static_cast<double>(meta->freq) / wm.future_timestamp;
                            wm.predict_data.next();
                        } else {
                            op.valid_time = 1000000;
                            op.priority = 0;
                        }
                    }
                    data_route_produce(op);
                } else if (update_mode == P2_BinaryModel) {
                    if (op.op == READ) {
                        wm.update_once(op, false);
                        wm2.update_once(op, false);
                    } else if (op.op == UPDATE) {
                        wm2.update_once(op, false);
                        auto meta = wm.req_metas[op.key];
                        auto req_meta = wm2.req_op_metas[op.key];
                        if (meta != nullptr) {
                            op.priority = op.priority = static_cast<double>(meta->freq) / wm.future_timestamp;
                        } else {
                            op.priority = 0;
                        }
                        if (req_meta != nullptr) {
                            wm2.predict_data.push_back(req_meta, op);
                            wm2.predict_data.rank();
                            op.next = wm2.predict_data.results[0];
                            wm2.predict_data.next();
                        } else {
                            op.next = 0;
                        }
                    }
                    data_route_produce(op);
                } else if (update_mode == B4_FrequencyBasedUpdate) {
                    if (op.op == READ) {
                        wm.update_once(op, false);
                    } else if (op.op == UPDATE) {
                        auto meta = wm.req_metas[op.key];
                        if (meta != nullptr) {
                            op.priority = static_cast<double>(meta->freq) / wm.future_timestamp;
                        }
                    }
                    data_route_produce(op);
                } else
#endif
                op.pi_latency_st = commit_st + (cnt-1) * send_interval;
                if (update_mode == P4_new_base) {
                    auto model_inference_st = steady_clock::now();
                    if (config::score_batch <= 1) {
                        if (op.op == UPDATE) {

                            workloadml::Score sc_;
                            if (init_train_finished && workloadml::g_score_trace.try_get(op.key, sc_)) {
                                op.priority = sc_;
                            } else {
                                sc_ = req_score->get_score(op.key);
                                op.priority = sc_;
                                if (init_train_finished) workloadml::g_score_trace.record(op.key, sc_);
                            }
                            if (req_score_placebo != nullptr) req_score_placebo->get_score(op.key);
                        }
                        data_route_produce(op);
                    } else {
                        if (op.op == UPDATE) {
                            update_idxes.push_back(op_buf.size());
                            keys_buf.push_back(op.key);
                        }
                        op_buf.push_back(op);
                        if (update_idxes.size() >= config::score_batch || op_buf.size() >= 2*config::score_batch) {
                            if (keys_buf.size() > 0) {
                                req_score->get_scores(keys_buf, scores);
                                for (int i = 0; i < update_idxes.size(); ++i) {
                                    op_buf[update_idxes[i]].priority = scores[i];
                                }
                            }
                            for (int i = 0; i < op_buf.size(); ++i) {
                                data_route_produce(op_buf[i]);
                                ++cnt_batch;
                            }
                            op_buf.clear();
                            keys_buf.clear();
                            update_idxes.clear();
                            scores.clear();
                        }
                    }
                    auto model_inference_ed = steady_clock::now();
                    auto model_inference_duration = duration_cast<microseconds>(model_inference_ed-model_inference_st).count();
                    model_inference_time += model_inference_duration;
                } else {
                    data_route_produce(op);
                }

            }

            if (config::score_batch > 1) {
                auto model_inference_st = steady_clock::now();
                if (keys_buf.size() > 0) {
                    req_score->get_scores(keys_buf, scores);
                    for (int i = 0; i < update_idxes.size(); ++i) {
                        op_buf[update_idxes[i]].priority = scores[i];
                    }
                }
                auto model_inference_ed = steady_clock::now();
                auto model_inference_duration = duration_cast<microseconds>(model_inference_ed-model_inference_st).count();
                model_inference_time += model_inference_duration;
                for (int i = 0; i < op_buf.size(); ++i) {
                    data_route_produce(op_buf[i]);
                    ++cnt_batch;
                }
                op_buf.clear();
                keys_buf.clear();
                update_idxes.clear();
                scores.clear();
            }
            printf("[score thread] receive cnt: %ld, commit cnt: %ld\n", cnt, cnt_batch);
#ifdef RUN_AFTER_SCORE
            {
                std::unique_lock<std::mutex> lock(score_finish_mtx);
                score_thread_finished = true;
                score_finish_cond.notify_one();
            }
#endif
        });

        if (config::operator_enable) operator_thread = thread([&] {

            workloads::OpMeta a;
            memset(dirty, 0, sizeof(int) * workloads::init_record_count);
            memset(last_cf, 0, sizeof(int) * workloads::init_record_count);
            memset(last_op, 0, sizeof(int) * workloads::init_record_count);
            while (config::process) {
                if (!operator_pc.need_consume()) {
                    break;
                }

                auto op = operator_pc.consume();
                if (operator_pc.consumed_stop_msg()) {
                    break;
                }
                auto opop = op.op;
                auto call_from = op.call_from;
                auto key = op.key;
                auto& state = dirty[key];
                if (call_from == 1 && opop == 6) {
                    ++cnt16;
                    if (dirty[key] == 1) {
                        if (last_op[key] == 5) {
                            if (last_cf[key] == 1) {
                                ++miss_read_cnt;
                            } else if (last_cf[key] == 2) {
                                ++hit_read_cnt;
                            }
                        }
                    }
                    last_cf[key] = 0;
                    last_op[key] = 0;
                    dirty[key] = 0;
                } else if (call_from == 1 && opop == 5) {
                    ++cnt15;
                    last_cf[key] = 1;
                    last_op[key] = 5;
                } else if (call_from == 2 && opop == 5) {
                    ++cnt25;
                    last_cf[key] = 2;
                    last_op[key] = 5;
                } else if (call_from == 1 && opop == 1) {
                    ++cnt11;
                } else if (opop == 2) {
                    dirty[key] = 1;
                }

            }

        });

        training_thread = thread([&] {

#ifdef OLD_MODE
            if (update_mode == P1_OfflineTrain || update_mode == P3_FreqScoreLearnValid) {
                unique_lock<mutex> lock(init_training_mtx);
                init_training_cond.wait(lock, [&] {return init_train_start;});
                cout << "start init training" << endl;
                wm.train_once();
                wm.predict_data.booster = wm.train_data.booster;
                finish_init_training();
            } else if (update_mode == P2_BinaryModel) {
                unique_lock<mutex> lock(init_training_mtx);
                init_training_cond.wait(lock, [&] {return init_train_start;});
                cout << "start init training" << endl;
                wm2.train_once();
                wm2.predict_data.booster = wm2.train_data.booster;
                finish_init_training();
            } else
#endif
            if (update_mode == P4_new_base && config::new_mode == NEW_MODE::distance && config::skip_init) {

                cout << "[skip_init] skip training; booster_load=" << config::booster_load_path << endl;
                if (config::booster_load_path.empty()) {
                    cout << "[skip_init] WARN: no booster, scores must come from --score_load" << endl;
                } else {

                    cout << "[skip_init] booster_load ignored (scores come from --score_load; compare the files offline if needed)" << endl;
                }
                finish_init_training();
            } else if (update_mode == P4_new_base && config::new_mode == NEW_MODE::distance) {
                unique_lock<mutex> lock(init_training_mtx);
                init_training_cond.wait(lock, [&] {return init_train_start;});
                cout << "start training new distance model" << endl;
                static_pointer_cast<ReqScoreDistance>(req_score)->train_once();

                if (!config::booster_save_path.empty()) {
                    bool ok = static_pointer_cast<ReqScoreDistance>(req_score)->save_booster_file(config::booster_save_path);
                    cout << "[booster] save " << config::booster_save_path << " ok=" << ok << endl;
                }
                if (!config::booster_load_path.empty()) {
                    bool ok = static_pointer_cast<ReqScoreDistance>(req_score)->load_booster_file(config::booster_load_path);
                    cout << "[booster] load " << config::booster_load_path << " ok=" << ok << endl;
                }
                if (req_score_placebo != nullptr) {
                    cout << "start training placebo model" << endl;
                    req_score_placebo->train_once();
                }
                finish_init_training();
            }
        });

        if (data_sharded()) {

            for (int sid = 0; sid < (int)data_pc_s.size(); ++sid) {
                data_shard_threads.emplace_back([this, sid] {
                    data_worker(sid);
                });
            }
        } else {
        data_thread = thread([&] {
            current_ts = 0;
            uint64_t cnt = 0;
            uint64_t last_cnt = 0;
            double last_duration;
#ifdef CONTINUOUS_LOG
            ofstream continuous_log(config::continuous_log_name);
#endif

            vector<workloads::OpMeta> read_ops;
            vector<workloads::OpMeta> update_ops;

            set<uint64_t> update_keys;
            update_keys.clear();
            read_ops.clear();
            update_ops.clear();
            int window_cnt = 0;
            vector<uint64_t> current_version;
            current_version.resize(workloads::init_record_count+10);
            uint64_t block_version_st = 0;
            uint64_t block_version_ed = 0;

            uint64_t rx_op_cnt = 0;

            auto send_interval = (config::send_ops) ? microseconds(int(1000000.0 / config::send_ops))
                                                    : microseconds(0);
            while(config::process) {
                if (!data_pc.need_consume()) {
                    break;
                }

                auto&& op = data_pc.consume();

                if (data_pc.consumed_stop_msg()) {
                    break;
                }

                if (config::rx_delay > 0 && init_train_finished) {
                    if (++rx_op_cnt % (uint64_t) (config::rx_batch > 0 ? config::rx_batch : 1) == 0) {

                        this_thread::sleep_for(chrono::microseconds((long long) config::rx_delay));
                    }
                }
                if (init_train_finished) {

                    op.pi_latency_st = latency_anchor + latency_op_cnt * send_interval;
                    ++latency_op_cnt;
                }
                auto request_process_st = steady_clock::now();
                if (update_mode != B3_BatchUpdate) {
                    auto st = steady_clock::now();
                    if (op.op == workloads::READ) {
                        auto ret = read(op);
                    } else {
                        update(op);
                    }
                    auto ed = steady_clock::now();
                    if (init_train_finished) {
                        latency_sum += duration_cast<nanoseconds>(ed - st).count();
                        op.pi_latency_ed = ed;
                        auto duration = duration_cast<microseconds>(op.pi_latency_ed-op.pi_latency_st).count();
                        pi_latency_list.push_back(duration);

                        if (op.op == workloads::READ) {
                            read_svc_latency_list.push_back(duration_cast<microseconds>(ed - st).count());
                        }
                        ++latency_cnt;
                    }
                } else {

                    std::vector<uint8_t> bytes;
                    auto batch_window = (config::ads_update_batch > 0 ? config::ads_update_batch : 1);
                    auto current_window = op.ts / batch_window;
                    if (op.op == READ) {
                        if (init_train_finished) {
                            op.proof_latency_st = steady_clock::now();
                            read_ops.push_back(op);
                            auto ret = index_->search(op.key);
                        }
                        ++read_cnt;

                    } else if (op.op == UPDATE) {
                        if (init_train_finished) {
                            current_version[op.key] = op.ts;
                            update_keys.insert(op.key);
                            update_ops.push_back(op);
                            index_->insert(op.key, op.value, false);
                        }
                        ++update_cnt;
                    }
                    if (current_window > window_cnt) {
                        window_cnt = current_window;
                        if (init_train_finished) {
                            auto st = steady_clock::now();

                            auto wait_maintenance_st = steady_clock::now();
                            index_->BatchUpdateHash(&update_keys);
                            auto wait_maintenance_ed = steady_clock::now();
                            auto wait_maintenance_duration = duration_cast<microseconds>(wait_maintenance_ed-wait_maintenance_st).count();

                            for (auto &op: read_ops) {
                                if (op.ts > current_version[op.key]) {
                                    index_->Prove(op.key, op.ts);
                                    op.proof_latency_ed = steady_clock::now();
                                } else {
                                    ++batch_loss_cnt;
                                }
                                op.pi_latency_ed = steady_clock::now();
                                auto duration = duration_cast<microseconds>(op.pi_latency_ed-op.pi_latency_st).count();
                                pi_latency_list.push_back(duration);
                                if (init_train_finished) {
                                    if (config::operator_enable) operator_pc.produce({op.key, op.op, READ_HASH, cpputils::getTimestamp()});
                                }
                            }

                            for (auto &op: update_ops) {
                                index_->Prove(op.key, op.ts);
                                if (init_train_finished) {
                                    if (config::operator_enable) operator_pc.produce({op.key, op.op, READ_HASH, cpputils::getTimestamp()});
                                }
                                op.pi_latency_ed = steady_clock::now();
                                auto duration = duration_cast<microseconds>(op.pi_latency_ed-op.pi_latency_st).count();
                                pi_latency_list.push_back(duration);
                            }
                            auto ed = steady_clock::now();

                            latency_sum += duration_cast<nanoseconds>(ed - st).count() * (read_ops.size() + update_ops.size());
                            latency_cnt += read_ops.size() + update_ops.size();
                            update_keys.clear();
                            read_ops.clear();
                            update_ops.clear();
                        }
                    }
                }
                auto request_process_ed = steady_clock::now();
                if (init_train_finished) {
                    auto request_process_duration = duration_cast<microseconds>(request_process_ed - request_process_st).count();
                    request_process_time += request_process_duration;
                }
                ++cnt;
                ++current_ts;
                if (cnt == init_training_cnt) {

                    if (update_mode == P1_OfflineTrain || update_mode == P3_FreqScoreLearnValid || update_mode == P2_BinaryModel || (update_mode == P4_new_base && config::new_mode == NEW_MODE::distance)) {
                        cout << "process " << cnt << " datas, reads: " << read_cnt << endl;
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

                }
#ifdef CONTINUOUS_LOG
                uint64_t cnt2;
                uint delta_cnt;
                if (init_train_finished && (cnt2 = cnt - config::init_training_cnt) % config::continuous_log_interval == 0) {
                    auto t = steady_clock::now();
                    auto duration = duration_cast<milliseconds>(t - start).count() / 1000.0;
                    delta_cnt = cnt2 - last_cnt;
                    continuous_log << std::fixed << std::setprecision(2) << cnt2 << "\t" << duration << "\t" << cnt2 / duration << "\t" << delta_cnt << "\t" << delta_cnt / (duration-last_duration) << endl;
                    last_cnt = cnt2;
                    last_duration = duration;
                }
#endif
            }

            if (update_mode == B3_BatchUpdate) {
                auto st = steady_clock::now();

                index_->BatchUpdateHash(&update_keys);

                for (auto &op: read_ops) {
                    if (op.ts > current_version[op.key]) {
                        index_->Prove(op.key, op.ts);
                        op.proof_latency_ed = steady_clock::now();
                    } else {
                        ++batch_loss_cnt;
                    }
                    if (init_train_finished) {
                        if (config::operator_enable) operator_pc.produce({op.key, op.op, READ_HASH, cpputils::getTimestamp()});
                    }
                }

                for (auto &op: update_ops) {
                    index_->Prove(op.key, op.ts);
                    if (init_train_finished) {
                        if (config::operator_enable) operator_pc.produce({op.key, op.op, READ_HASH, cpputils::getTimestamp()});
                    }
                }
                auto ed = steady_clock::now();

                latency_sum += duration_cast<nanoseconds>(ed - st).count() * read_ops.size();
                latency_cnt += read_ops.size();
                update_keys.clear();
                read_ops.clear();
            }
            cout << "data thread finished" << endl;
        });
        }
    }
}