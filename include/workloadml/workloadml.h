
#ifndef INDEX_MAINTAIN_WORKLOADML_H
#define INDEX_MAINTAIN_WORKLOADML_H

#include <vector>
#include <string>
#include <set>
#include <map>
#include <unordered_map>
#include <cmath>
#include <ctime>
#include <sys/time.h>
#include <list>
#include <memory>
#include <LightGBM/c_api.h>
#include <chrono>
#include "workloads/ycsb_workload.h"
#include "cpputils/big_vector.h"
#include "config.h"

using namespace workloads;

namespace workloadml {

    extern uint time_window_size;
    extern uint8_t max_n_past_timestamps;
    extern uint max_n_past_distances;

    class IndexOpMeta {

    };

    class ReqOpMeta {
    public:
        std::vector<uint8_t> seqs;
        std::vector<int64_t> timestamps;
        int64_t sum;
        uint64_t cnt;

        void push_back(uint8_t op, int64_t ts) {
            seqs.push_back(op);
            timestamps.push_back(ts);
            sum += op;
            ++cnt;
        }
    };

    class ReqOpModel {

    public:

        std::vector<float> labels;
        std::vector<int32_t> indptr;
        std::vector<int32_t> indices;
        std::vector<double> data;

        std::vector<double> training_losses;
        std::vector<uint64_t> key_list;
        std::vector<double> scores;
        std::vector<uint8_t> results;
        BoosterHandle booster = nullptr;

        int n_feature = max_n_past_distances;
        int num_iterations = 100;
        const char *training_params = "boosting=gbdt\n"
                                      "objective=binary\n"

                                      "num_iterations=100\n"

                                      "num_leaves=32\n"

                                      "num_threads=4\n"
                                      "feature_fraction=0.8\n"
                                      "bagging_freq=5\n"
                                      "bagging_fraction=0.8\n"
                                      "learning_rate=0.1\n"
                                      "verbosity=0\n";

        ReqOpModel(int train_batch_size, int rank_batch_size) {
            labels.reserve(train_batch_size);
            indptr.reserve(train_batch_size + 1);
            indptr.push_back(0);
            indices.reserve(train_batch_size * n_feature);
            data.reserve(train_batch_size * n_feature);
            key_list.reserve(train_batch_size);

            scores.resize(rank_batch_size+10);
            results.reserve(rank_batch_size+10);
        }

        void next() {
            labels.clear();
            indices.clear();
            data.clear();
            indptr.clear();
            indptr.push_back(0);
            key_list.clear();
            results.clear();

        }

        void push_back(std::shared_ptr<ReqOpMeta>& req_op_meta, workloads::OpMeta &op_meta, bool train = false) {

            int row_idx = 0;
            if (req_op_meta != nullptr) {
                for (int i = 0; i < max_n_past_distances && i < req_op_meta->seqs.size(); ++i, ++row_idx) {
                    auto past_distance_idx = req_op_meta->seqs.size() - 1 - i;

                    auto seq_i = req_op_meta->seqs[past_distance_idx];

                    indices.push_back(row_idx);
                    data.push_back(seq_i);

                }
            }
            row_idx = max_n_past_distances;

            assert(row_idx == n_feature);
            indptr.push_back(data.size());

            if (train) {
                labels.push_back(op_meta.next);
            }
            key_list.push_back(op_meta.key);
        }

        bool show_train_loss = false;
        void train() {

            if(booster) booster=nullptr;
            DatasetHandle trainData;
            LGBM_DatasetCreateFromCSR(
                    static_cast<void *>(this->indptr.data()),
                    C_API_DTYPE_INT32,
                    this->indices.data(),
                    static_cast<void *>(this->data.data()),
                    C_API_DTYPE_FLOAT64,
                    this->indptr.size(),
                    this->data.size(),
                    n_feature,
                    training_params,
                    nullptr,
                    &trainData);

            LGBM_DatasetSetField(trainData,
                                 "label",
                                 static_cast<void *>(this->labels.data()),
                                 this->labels.size(),
                                 C_API_DTYPE_FLOAT32);
            LGBM_BoosterCreate(trainData, training_params, &booster);

            for (int i = 0; i < num_iterations; i++) {
                int isFinished;
                LGBM_BoosterUpdateOneIter(booster, &isFinished);
                if (isFinished) {
                    break;
                }
            }

            if (show_train_loss) {

                int64_t len;
                std::vector<double> result(this->indptr.size());
                LGBM_BoosterPredictForCSR(booster,
                                          static_cast<void *>(this->indptr.data()),
                                          C_API_DTYPE_INT32,
                                          this->indices.data(),
                                          static_cast<void *>(this->data.data()),
                                          C_API_DTYPE_FLOAT64,
                                          this->indptr.size(),
                                          this->data.size(),
                                          n_feature,
                                          C_API_PREDICT_NORMAL,
                                          0, 0,
                                          training_params,
                                          &len,
                                          result.data());
                double se = 0;
                for (int i = 0; i < result.size(); ++i) {
                    auto diff = result[i] - this->labels[i];
                    se += diff * diff;
                }
                training_losses.push_back(se / len);
                std::cout << "loss: " << se / len << std::endl;

            }

            LGBM_DatasetFree(trainData);

        }

        void predict() {
            auto inference_params = training_params;
            int64_t len;
            std::vector<double> result(this->indptr.size());
            std::vector<double> losses;
            LGBM_BoosterPredictForCSR(booster,
                                      static_cast<void *>(indptr.data()),
                                      C_API_DTYPE_INT32,
                                      indices.data(),
                                      static_cast<void *>(data.data()),
                                      C_API_DTYPE_FLOAT64,
                                      this->indptr.size(),
                                      this->data.size(),
                                      n_feature,
                                      C_API_PREDICT_NORMAL,
                                      0, 0,
                                      inference_params,
                                      &len,
                                      result.data());
            double se = 0;
            int cnt = 0;
            for (int i = 0; i < len; ++i) {
                auto diff = result[i] - this->labels[i];
                se += diff * diff;
                ++cnt;
            }
            losses.push_back(se / len);
            std::cout << "cnt: " << cnt << std::endl;
            std::cout << "loss: " << se / len << std::endl;
        }

        void rank() {
            auto inference_params = training_params;
            int64_t len;

            LGBM_BoosterPredictForCSR(booster,
                                      static_cast<void *>(indptr.data()),
                                      C_API_DTYPE_INT32,
                                      indices.data(),
                                      static_cast<void *>(data.data()),
                                      C_API_DTYPE_FLOAT64,
                                      this->indptr.size(),
                                      this->data.size(),
                                      n_feature,
                                      C_API_PREDICT_NORMAL,
                                      0, 0,
                                      inference_params,
                                      &len,
                                      scores.data());
            for (int i = 0; i < len; ++i) {
                results.push_back(((scores[i] >= 0.50) ? 1 : 0));
            }
        }
    };

    class WM2 {
    public:
        std::vector<std::shared_ptr<ReqOpMeta>> req_op_metas;
        ReqOpModel train_data, predict_data;

        WM2(uint64_t init_size, int train_bc, int rank_bc): train_data(train_bc, 0),
                                                            predict_data(rank_bc, rank_bc) {
            req_op_metas.resize(init_size);
        }

        void update_once(workloads::OpMeta op_meta, bool prepare_train_data) {
            auto& req_op_meta = req_op_metas[op_meta.key];
            if (req_op_meta == nullptr) {
                req_op_meta = std::make_shared<ReqOpMeta>();
            }

            uint8_t seq = 0;
            if (op_meta.op == READ) {
                seq = 1;
            } else if (op_meta.op == UPDATE) {
                seq = 0;
            } else {
                assert(false);
            }
            req_op_meta->push_back(seq, op_meta.ts);

            if (prepare_train_data && op_meta.ts >= time_window_size) {
                train_data.push_back(req_op_meta, op_meta, true);
            }
        }

        void train_once() {
            auto st = std::chrono::steady_clock::now();
            std::cout << "train_mode data size: " << train_data.labels.size() << " " << train_data.key_list.size() << std::endl;
            train_data.train();
            auto ed = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(ed - st).count();
            std::cout << "training time(ms): " << duration << std::endl;
            train_data.next();
        }
    };

    class ReqMeta {
    public:
        int64_t last_timestamp = -1;
        uint64_t freq;
        std::vector<uint32_t> deltas;
        std::list<int64_t> past_timestamps;

        void update(int64_t current_timestamp) {
            if (last_timestamp != -1) {
                auto interval = current_timestamp - last_timestamp;
                deltas.push_back(interval);
                past_timestamps.push_back(last_timestamp);

            }
            last_timestamp = current_timestamp;
            freq++;

        }
    };

    class BoostData {

    public:

        std::vector<float> labels;
        std::vector<int32_t> indptr;
        std::vector<int32_t> indices;
        std::vector<double> data;

        std::vector<double> training_losses;
        std::vector<uint64_t> key_list;
        std::vector<double> scores;
        BoosterHandle booster = nullptr;

#ifndef MODEL_WITH_FREQ
        int n_feature = max_n_past_distances + 2 - 2;
#else
        int n_feature = max_n_past_distances + 2;
#endif

        int train_batch_size = 131072;
        int rank_batch_size = 1000;
        int num_iterations = 32;

        const char *training_params = "boosting=gbdt\n"
                                      "objective=regression\n"
                                      "num_iterations=32\n"

                                      "num_leaves=32\n"

                                      "num_threads=4\n"
                                      "feature_fraction=0.8\n"
                                      "bagging_freq=5\n"
                                      "bagging_fraction=0.8\n"
                                      "learning_rate=0.1\n"
                                      "verbosity=0\n";

        BoostData(int train_batch_size, int rank_batch_size): train_batch_size(train_batch_size), rank_batch_size(rank_batch_size) {
            labels.reserve(train_batch_size);
            indptr.reserve(train_batch_size + 1);
            indptr.push_back(0);
            indices.reserve(train_batch_size * n_feature);
            data.reserve(train_batch_size * n_feature);
            key_list.reserve(train_batch_size);

            scores.resize(rank_batch_size+10);
        }

        void next() {
            labels.clear();
            indices.clear();
            data.clear();
            indptr.clear();
            indptr.push_back(0);
            key_list.clear();

        }

        void push_back(std::shared_ptr<ReqMeta>& future_req_meta, uint64_t key, int64_t timestamp, uint32_t future_distance = 0, bool train = false) {
            uint64_t total_distance = 0;
            uint8_t n_within = 0;

            int row_idx = 0;
            if (future_req_meta != nullptr) {

                for (int i = 0; i < max_n_past_distances && i < future_req_meta->deltas.size(); ++i, ++row_idx) {

                    auto past_distance_idx = future_req_meta->deltas.size() - 1 - i;
                    auto delta_i = future_req_meta->deltas[past_distance_idx];
                    total_distance += delta_i;
                    indices.push_back(row_idx);
                    data.push_back(delta_i);
                    if (total_distance < time_window_size) {
                        ++n_within;
                    }
                }
            } else {

            }
            row_idx = max_n_past_distances;

#ifdef MODEL_WITH_FREQ
            indices.push_back(row_idx);
            data.push_back(n_within);
            ++row_idx;

            indices.push_back(row_idx);
            if (future_req_meta != nullptr) {

                data.push_back(static_cast<double>(future_req_meta->freq) / timestamp);
            } else {
                data.push_back(0.0);
            }
            ++row_idx;
#endif

            assert(row_idx == n_feature);
            indptr.push_back(data.size());

            if (train) {
                assert(future_distance != 0);

                labels.push_back(future_distance);
            }
            key_list.push_back(key);
        }

        bool show_train_loss = false;
        void train() {

            if(booster) booster=nullptr;
            DatasetHandle trainData;
            LGBM_DatasetCreateFromCSR(
                    static_cast<void *>(this->indptr.data()),
                    C_API_DTYPE_INT32,
                    this->indices.data(),
                    static_cast<void *>(this->data.data()),
                    C_API_DTYPE_FLOAT64,
                    this->indptr.size(),
                    this->data.size(),
                    n_feature,
                    training_params,
                    nullptr,
                    &trainData);

            LGBM_DatasetSetField(trainData,
                                 "label",
                                 static_cast<void *>(this->labels.data()),
                                 this->labels.size(),
                                 C_API_DTYPE_FLOAT32);
            LGBM_BoosterCreate(trainData, training_params, &booster);

            for (int i = 0; i < num_iterations; i++) {
                int isFinished;
                LGBM_BoosterUpdateOneIter(booster, &isFinished);
                if (isFinished) {
                    break;
                }
            }

            if (show_train_loss) {

                int64_t len;
                std::vector<double> result(this->indptr.size());
                LGBM_BoosterPredictForCSR(booster,
                                          static_cast<void *>(this->indptr.data()),
                                          C_API_DTYPE_INT32,
                                          this->indices.data(),
                                          static_cast<void *>(this->data.data()),
                                          C_API_DTYPE_FLOAT64,
                                          this->indptr.size(),
                                          this->data.size(),
                                          n_feature,
                                          C_API_PREDICT_NORMAL,
                                          0, 0,
                                          training_params,
                                          &len,
                                          result.data());
                double se = 0;
                for (int i = 0; i < result.size(); ++i) {
                    auto diff = result[i] - this->labels[i];
                    se += diff * diff;
                }
                training_losses.push_back(se / len);
                std::cout << "loss: " << se / len << std::endl;

            }

            LGBM_DatasetFree(trainData);

        }

        void predict() {
            auto inference_params = training_params;
            int64_t len;
            std::vector<double> result(this->indptr.size());
            std::vector<double> losses;
            LGBM_BoosterPredictForCSR(booster,
                                      static_cast<void *>(indptr.data()),
                                      C_API_DTYPE_INT32,
                                      indices.data(),
                                      static_cast<void *>(data.data()),
                                      C_API_DTYPE_FLOAT64,
                                      this->indptr.size(),
                                      this->data.size(),
                                      n_feature,
                                      C_API_PREDICT_NORMAL,
                                      0, 0,
                                      inference_params,
                                      &len,
                                      result.data());
            double se = 0;
            int cnt = 0;
            for (int i = 0; i < len; ++i) {
                auto diff = result[i] - this->labels[i];
                se += diff * diff;
                ++cnt;
            }
            losses.push_back(se / len);
            std::cout << "cnt: " << cnt << std::endl;
            std::cout << "loss: " << se / len << std::endl;
        }

        void rank() {
            auto inference_params = training_params;
            int64_t len;

            LGBM_BoosterPredictForCSR(booster,
                                      static_cast<void *>(indptr.data()),
                                      C_API_DTYPE_INT32,
                                      indices.data(),
                                      static_cast<void *>(data.data()),
                                      C_API_DTYPE_FLOAT64,
                                      this->indptr.size(),
                                      this->data.size(),
                                      n_feature,
                                      C_API_PREDICT_NORMAL,
                                      0, 0,
                                      inference_params,
                                      &len,
                                      scores.data());

            std::unordered_map<uint64_t, int> index;

        }

        void show(std::string &&name);
    };

    enum ADSMaintainMode {
        Immediate,
        Frequency,
        Boost,
    };

    class WorkloadMonitor {
    public:

        int64_t future_timestamp = 0;
        std::vector<std::shared_ptr<ReqMeta>> req_metas;

        ADSMaintainMode mode;

        BoostData train_data, predict_data;

    public:
        explicit WorkloadMonitor(uint64_t init_size, int train_bc, int rank_bc, bool wr_both, ADSMaintainMode mode = Frequency): mode(mode),
                                                                                                                   train_data(train_bc, 0),
                                                                                                                   predict_data(rank_bc, rank_bc) {
            if (!wr_both) {
                req_metas.resize(init_size);
            } else {
                req_metas.resize(init_size * 2);
            }
        }

        void set_mode(ADSMaintainMode mode) {
            this->mode = mode;
        }

        bool update_once(workloads::OpMeta &future_op, bool prepare_train_data) {
            bool trained = false;

            auto& future_req_meta = req_metas[future_op.key];
            if (future_req_meta == nullptr) {
                future_req_meta = std::make_shared<ReqMeta>();
            }

            if (prepare_train_data && future_timestamp >= time_window_size) {
                uint32_t future_distance;
                if (future_req_meta->last_timestamp == -1) {
                    future_distance = 2 * time_window_size;
                } else {
                    future_distance = future_timestamp - future_req_meta->last_timestamp;
                }
                train_data.push_back(future_req_meta, future_op.key, future_timestamp, future_distance, true);
            }

            auto& now_op = future_op;
            auto& now_meta = req_metas[now_op.key];
            auto& now_timestamp = future_timestamp;
            now_meta->update(now_timestamp);
            ++future_timestamp;

            return trained;
        }

        void train_once() {

            auto st = std::chrono::steady_clock::now();
            std::cout << "train_mode data size: " << train_data.labels.size() << " " << train_data.key_list.size() << std::endl;
            train_data.train();
            auto ed = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(ed - st).count();
            std::cout << "training time(ms): " << duration << std::endl;
            train_data.next();

        }

    };

}

#endif
