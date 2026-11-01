
#ifndef INDEX_MAINTAIN_NEW_DISTANCE_H
#define INDEX_MAINTAIN_NEW_DISTANCE_H

#include "workloadml/new_base.h"
#include "config.h"
#include <LightGBM/c_api.h>
#include <iostream>
#include <vector>
#include <queue>
#include <deque>
#include <unordered_map>
#include <thread>
#include <fstream>
#include <iterator>
#include <cstdlib>
#include "cpputils/size.h"

namespace workloadml {
    enum LearningScoreOption {
        DeltaOnly=0,
        ScoreWithFreq=1,
        LearningWithFreq=2,
        LearningScoreWithFreq=3,

    };
    class DistanceMeta {
    public:
        std::deque<uint32_t> deltas;
        uint64_t last_ts;
        int n_within = 0;
        uint64_t n_all = 0;

        uint64_t last_any_ts = 0;
        bool has_write = false;
        uint64_t last_write_ts = 0;
        uint64_t n_write_all = 0;
        int n_write_within = 0;
        float edc[4] = {1, 1, 1, 1};
        uint64_t edc_ts = 0;

        uint64_t pred_n_all = 0;
        double pred_cache = -1;
        int64_t pred_epoch = -1;

#ifdef ANOMALY
        uint64_t sum=0;
        double ave=0;
        double std_dev=0;
#endif
        uint64_t mem_size() {
            if (n_within == 0) {
                return sizeof(int);
            }
            return deltas.size() * sizeof(uint32_t) + sizeof(*this);
        }
    };

    struct FSExtra {
        double since_any = -1;
        double log_n_read = 0;
        double density = 0;
        int has_hist = 0;
        double edc[4] = {0, 0, 0, 0};
        double since_write = -1;
        int n_write_within = 0;

        double n_within_log = 0;
        double mean_gap_win_log = -1;
        double mean_gap_all_log = 0;
        double rel_rec_all_log = -1;
        double rel_rec_win_log = -1;
    };

    inline int fs_dims(int fs, int D) {
        switch (fs) {
            case 1: return 2 + D;
            case 2: return 3 + D;
            case 3: return 5 + D;
            case 4: return 7 + D;
            case 5: return 11 + D;
            case 6: return 13 + D;
            case 7: case 8: return 11 + D;
            case 9: case 10: return 12 + D;
            case 11: return 2 + D;

            case 12: case 13: return 1;
            case 14: case 15: return 1 + D;
            case 16: return 2;
            case 17: return D;
            default: return D;
        }
    }

    inline void compute_fs_features(const DistanceMeta *m, uint64_t ts, int64_t window, FSExtra *out) {
        if (m == nullptr || out == nullptr) return;
        out->since_any = (ts >= m->last_any_ts) ? std::log1p((double) (ts - m->last_any_ts)) : -1.0;
        out->log_n_read = std::log1p((double) m->n_all);
        out->density = std::log1p((double) ts / (double) (m->n_all + 1));
        out->has_hist = (m->n_all > 0) ? 1 : 0;
        double dt = (m->edc_ts > 0 && ts > m->edc_ts) ? (double) (ts - m->edc_ts) : 0.0;
        for (int j = 0; j < 4; ++j) {
            double tau = (double) window * std::pow(4.0, j - 1);
            if (tau < 1.0) tau = 1.0;
            double v = (double) m->edc[j] * std::exp(-dt / tau);
            out->edc[j] = std::log1p(v > 0.0 ? v : 0.0);
        }
        out->since_write = (m->has_write && ts >= m->last_write_ts)
                           ? std::log1p((double) (ts - m->last_write_ts)) : -1.0;
        out->n_write_within = m->n_write_within;

        const double sr0 = (ts >= m->last_ts) ? (double) (ts - m->last_ts) : 0.0;
        out->n_within_log = std::log1p((double) (m->n_within > 0 ? m->n_within : 0));
        const double mgw = (m->n_within > 0 && window > 0) ? (double) window / (double) m->n_within : -1.0;
        const double mga = (double) ts / (double) (m->n_all + 1);
        out->mean_gap_win_log = (mgw > 0.0) ? std::log1p(mgw) : -1.0;
        out->mean_gap_all_log = std::log1p(mga);
        out->rel_rec_all_log = (mga > 0.0) ? std::log1p(sr0 / mga) : -1.0;
        out->rel_rec_win_log = (mgw > 0.0) ? std::log1p(sr0 / mgw) : -1.0;
    }

    extern const char *training_params;

    extern std::string training_params_override;
    extern int train_label_mode;
    extern int train_label_horizon;

    extern int inference_num_threads;

    class DistanceModel {

        std::vector<float> labels;
        std::vector<int32_t> indptr;
        std::vector<int32_t> indices;
        std::vector<double> data;

        std::vector<double> training_losses;
        std::vector<uint64_t> key_list;
        std::vector<double> scores;
        BoosterHandle booster = nullptr;

        int n_feature, num_iterations;
        const char *get_params() const {

            if (!training_params_override.empty()) return training_params_override.c_str();
            return params_det.empty() ? training_params : params_det.c_str();
        }

        std::string params_det;
        int64_t label_cap = 0;
        bool show_train_loss;

    public:
        uint64_t mem_size() {
            return labels.size() * sizeof(float)
            + indptr.size() * sizeof(int32_t)
            + indices.size() * sizeof(int32_t)
            + data.size() * sizeof(double)
            + training_losses.size() * sizeof(double)
            + key_list.size() * sizeof(uint64_t)
            + scores.size() * sizeof(double)
            + sizeof(*this);
        }

        uint64_t mem_size_valid() {
            return indptr.size() * sizeof(int32_t)
                   + indices.size() * sizeof(int32_t)
                   + data.size() * sizeof(double)
                   + key_list.size() * sizeof(uint64_t)
                   + sizeof(*this);
        }

        ~DistanceModel() {
            auto size = mem_size_valid();
            std::cout << "[DistanceScoreData] memory consumption: " << cpputils::getSizeStr(size) << std::endl;
        }

        DistanceModel() {

            n_feature = (config::lr_feature_set > 0)
                        ? fs_dims(config::lr_feature_set, config::max_delta_cnt)
                        : config::max_delta_cnt;
            num_iterations = 32;

            {
                static const bool lr_det = (std::getenv("LR_DET") != nullptr);
                static const bool lr_det_1t = (std::getenv("LR_DET_THREADS") != nullptr);
                if (lr_det) {
                    params_det = std::string(training_params)
                                 + "\ndeterministic=true\nforce_row_wise=true\nseed=1\n";
                    if (lr_det_1t) params_det += "num_threads=1\n";
                }
            }

            show_train_loss = (config::learning_score_option & 512) != 0;
            if (config::lr_feature_set == 0) {

                if ((config::learning_score_option & 2) != 0) {
                    n_feature += 1;
                }

                if ((config::learning_score_option & 16) != 0) {
                    n_feature += 1;
                }
                if ((config::learning_score_option & 32) != 0) {
                    n_feature += 1;
                }
            }
            indptr.push_back(0);
            scores.resize(10);
        }

        void next() {
            labels.clear();
            indices.clear();
            data.clear();
            indptr.clear();
            indptr.push_back(0);
            key_list.clear();
        }

        bool push_back(std::shared_ptr<DistanceMeta> req_meta, uint64_t key, double freq, int64_t label = 0, bool train = false,
                       int64_t recency = 0, int n_within_feat = -1,
                       const std::vector<uint32_t> *deltas_snapshot = nullptr,
                       const FSExtra *fs_ex = nullptr) {
            int row_idx = 0;
            std::vector<uint32_t> deltas_tmp;
            const std::vector<uint32_t> *deltas_ptr = deltas_snapshot;
            if (deltas_ptr == nullptr && req_meta != nullptr) {
                deltas_tmp.assign(req_meta->deltas.begin(), req_meta->deltas.end());
                deltas_ptr = &deltas_tmp;
            }
            size_t ndeltas = (deltas_ptr != nullptr) ? deltas_ptr->size() : 0;

            const int fs = config::lr_feature_set;

            const int fl = (fs >= 7 && fs <= 10) ? 5 : ((fs == 11 || (fs >= 12 && fs <= 17)) ? 1 : fs);

            bool abl_sr = true, abl_dl = true, abl_nw = true;
            if (fs >= 12 && fs <= 17) {
                abl_sr = (fs == 13 || fs == 15 || fs == 16);
                abl_dl = (fs == 14 || fs == 15 || fs == 17);
                abl_nw = (fs == 12 || fs == 14 || fs == 16);
            }
            if (fs > 0) {

                if (abl_sr) {
                    indices.push_back(row_idx);
                    data.push_back(static_cast<double>(recency));
                    ++row_idx;
                }
                if (abl_dl) {
                    int i = 0;
                    for (; i < config::max_delta_cnt && (size_t) i < ndeltas; ++i, ++row_idx) {
                        indices.push_back(row_idx);
                        data.push_back(static_cast<double>((*deltas_ptr)[i]));
                    }
                    row_idx += (config::max_delta_cnt - i);
                }

                double nw_val;
                if (fs == 7)      nw_val = fs_ex ? fs_ex->n_within_log : 0.0;
                else if (fs == 8) nw_val = fs_ex ? fs_ex->mean_gap_win_log : -1.0;
                else if (fs == 11) nw_val = fs_ex ? fs_ex->density : -1.0;
                else              nw_val = static_cast<double>(n_within_feat < 0 ? 0 : n_within_feat);
                if (abl_nw) {
                    indices.push_back(row_idx);
                    data.push_back(nw_val);
                    ++row_idx;
                }
                if (fl >= 2) {
                    indices.push_back(row_idx);
                    data.push_back(fs_ex ? fs_ex->since_any : -1.0);
                    ++row_idx;
                }
                if (fl >= 3) {
                    indices.push_back(row_idx);
                    data.push_back(fs_ex ? fs_ex->log_n_read : 0.0);
                    ++row_idx;
                    indices.push_back(row_idx);
                    data.push_back(fs_ex ? fs_ex->density : 0.0);
                    ++row_idx;
                }
                if (fl >= 4) {
                    auto nd = (int) std::min<size_t>(ndeltas, (size_t) config::max_delta_cnt);
                    indices.push_back(row_idx);
                    data.push_back(static_cast<double>(nd));
                    ++row_idx;
                    indices.push_back(row_idx);
                    data.push_back(fs_ex ? static_cast<double>(fs_ex->has_hist) : 0.0);
                    ++row_idx;
                }
                if (fl >= 5) {
                    for (int j = 0; j < 4; ++j) {
                        indices.push_back(row_idx);
                        data.push_back(fs_ex ? fs_ex->edc[j] : 0.0);
                        ++row_idx;
                    }
                }
                if (fl >= 6) {
                    indices.push_back(row_idx);
                    data.push_back(fs_ex ? fs_ex->since_write : -1.0);
                    ++row_idx;
                    indices.push_back(row_idx);
                    data.push_back(fs_ex ? static_cast<double>(fs_ex->n_write_within) : 0.0);
                    ++row_idx;
                }
                if (fs == 9 || fs == 10) {
                    indices.push_back(row_idx);
                    data.push_back(fs_ex ? (fs == 9 ? fs_ex->rel_rec_all_log : fs_ex->rel_rec_win_log) : -1.0);
                    ++row_idx;
                }
            } else {

            if ((config::learning_score_option & 32) != 0) {
                indices.push_back(row_idx);
                data.push_back(static_cast<double>(recency));
                ++row_idx;
            }

            int i = 0;
            for (i = 0; i < config::max_delta_cnt && (size_t) i < ndeltas; ++i, ++row_idx) {
                auto delta = (*deltas_ptr)[i];
#ifdef ANOMALY
                if ((config::learning_score_option == 8) && req_meta != nullptr && (delta > req_meta->ave+3*req_meta->std_dev || delta < req_meta->ave-3*req_meta->std_dev)) {
                    --i;
                    --row_idx;
                    continue;
                }
#endif
                indices.push_back(row_idx);
                data.push_back(delta);
            }
#ifdef ANOMALY
            if (i == 0) {
                if (train) {
                    return false;
                } else {
                    auto m = 0;
                    for (size_t t = 0; t < ndeltas; ++t) {
                        const auto &item = (*deltas_ptr)[t];
                        if (item > m) {
                            m = item;
                        }
                    }
                    indices.push_back(row_idx);
                    data.push_back(m);

                }
            }
#endif
            row_idx += (config::max_delta_cnt - i);

            if ((config::learning_score_option & 16) != 0) {
                indices.push_back(row_idx);
                data.push_back(static_cast<double>(n_within_feat));
                ++row_idx;
            }

            if ((config::learning_score_option & 2) != 0) {
                indices.push_back(row_idx);
                data.push_back(-freq);
                ++row_idx;
            }
            }

            if (row_idx != n_feature) {
                puts("row_idx != n_feature");
                exit(-1);
            }
            indptr.push_back(data.size());
            if (train) {

                if (train_label_mode != 0) {
                    labels.push_back(label <= train_label_horizon ? 1.0f : 0.0f);
                } else {

                if (label_cap > 0 && label > label_cap) {
                    label = label_cap;
                }
                labels.push_back(log1p(label));
                }
            }
            key_list.push_back(key);
            return true;
        }

        void train() {
            std::cout << "data size: " << data.size() << std::endl;
            std::cout << "indices size: " << indices.size() << std::endl;
            std::cout << "indptr size: " << indptr.size() << std::endl;
            std::cout << "labels size: " << labels.size() << std::endl;

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
                    get_params(),
                    nullptr,
                    &trainData);

            LGBM_DatasetSetField(trainData,
                                 "label",
                                 static_cast<void *>(this->labels.data()),
                                 this->labels.size(),
                                 C_API_DTYPE_FLOAT32);
            LGBM_BoosterCreate(trainData, get_params(), &booster);

            for (int i = 0; i < num_iterations; i++) {
                int isFinished;
                LGBM_BoosterUpdateOneIter(booster, &isFinished);
                if (isFinished) {
                    break;
                }
            }

            {
                std::string ms = save_booster_to_string();
                uint64_t h = 1469598103934665603ULL;
                for (unsigned char c: ms) { h ^= c; h *= 1099511628211ULL; }
                std::cout << "[model] fnv=" << std::hex << h << std::dec
                          << " len=" << ms.size() << std::endl;
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
                                          get_params(),
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

        int64_t rank() {

            std::string params_buf;
            const char *inference_params;
            int nth = (inst_infer_threads > 0) ? inst_infer_threads : inference_num_threads;
            if (nth > 0) {
                params_buf = get_params();
                size_t pos = params_buf.find("num_threads=");
                if (pos != std::string::npos) {
                    size_t eol = params_buf.find('\n', pos);
                    params_buf.replace(pos, eol - pos, "num_threads=" + std::to_string(nth));
                } else {
                    params_buf += "num_threads=" + std::to_string(nth) + "\n";
                }
                inference_params = params_buf.c_str();
            } else {
                inference_params = get_params();
            }

            if (scores.size() < indptr.size() - 1) {
                scores.resize(indptr.size() - 1);
            }
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
            return len;
        }

        friend class ReqScoreDistance;
    public:

        int inst_infer_threads = -1;

        bool save_booster_to_file(const std::string &p) {
            std::string ms = save_booster_to_string();
            std::ofstream f(p, std::ios::binary);
            if (!f) return false;
            f.write(ms.data(), (std::streamsize) ms.size());
            return (bool) f;
        }
        bool load_booster_from_file(const std::string &p) {
            std::ifstream f(p, std::ios::binary);
            if (!f) return false;
            std::string ms((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            return load_booster_from_string(ms);
        }

        std::string save_booster_to_string() {
            int64_t out_len = 0;
            std::vector<char> buf(64u << 20);
            LGBM_BoosterSaveModelToString(booster, 0, -1, 0, (int64_t) buf.size(), &out_len, buf.data());
            return std::string(buf.data());
        }
        bool load_booster_from_string(const std::string &s) {
            int out_iters = 0;
            if (booster) booster = nullptr;
            return LGBM_BoosterLoadModelFromString(s.c_str(), &out_iters, &booster) == 0;
        }
    };

    class ScoreTrace {
    public:
        struct Rec { uint64_t key; double s1, s2; };
        std::vector<Rec> rec;
        size_t cursor = 0;
        bool loading = false, inited = false;

        void ensure_init() {
            if (inited) return;
            inited = true;
            if (config::score_load_path.empty()) return;
            std::ifstream f(config::score_load_path, std::ios::binary);
            if (!f) { std::cerr << "[trace] cannot open " << config::score_load_path << std::endl; std::exit(1); }
            char m[8]; f.read(m, 8);
            if (std::string(m, 8) != "SFTR0001") { std::cerr << "[trace] bad magic" << std::endl; std::exit(1); }
            uint64_t n = 0; f.read((char *) &n, 8);
            rec.resize(n);
            f.read((char *) rec.data(), (std::streamsize) (n * sizeof(Rec)));
            if (!f) { std::cerr << "[trace] truncated" << std::endl; std::exit(1); }
            loading = true;
            std::cout << "[trace] loaded " << n << " records from " << config::score_load_path << std::endl;
        }

        bool try_get(uint64_t key, Score &out) {
            ensure_init();
            if (!loading) return false;
            if (cursor >= rec.size() || rec[cursor].key != key) {
                std::cerr << "[trace] FATAL miss cursor=" << cursor << "/" << rec.size()
                          << " key=" << key << std::endl;
                std::exit(3);
            }
            out = Score(rec[cursor].s1, rec[cursor].s2);
            ++cursor;
            return true;
        }

        void record(uint64_t key, const Score &s) {
            ensure_init();
            if (!config::score_dump_path.empty()) rec.push_back(Rec{key, s.score1, s.score2});
        }

        void finish() {
            if (inited && !config::score_dump_path.empty()) {
                std::ofstream f(config::score_dump_path, std::ios::binary);
                uint64_t n = rec.size();
                f.write("SFTR0001", 8);
                f.write((const char *) &n, 8);
                f.write((const char *) rec.data(), (std::streamsize) (n * sizeof(Rec)));
                std::cout << "[trace] dumped " << n << " records to " << config::score_dump_path << std::endl;
            }
            if (loading) std::cout << "[trace] consumed " << cursor << "/" << rec.size() << ", miss=0" << std::endl;
        }
    };
    static ScoreTrace g_score_trace;

    class ReqScoreDistance: public ReqScore {
    public:
        bool train_data_flag = false;
        std::vector<std::shared_ptr<DistanceMeta>> metas;

        DistanceModel model_train, model_score;

        std::queue<workloads::OpMeta> req_buf;
        int window_size = 0;

        std::mutex buf_mtx;
        uint64_t ts_ = 0;
        uint64_t N_within = 0;
        uint64_t N_all = 0;

        struct PendingSample {
            uint64_t ts = 0;
            int64_t recency = 0;
            int n_within = 0;
            double freq = 0;
            std::vector<uint32_t> deltas;
            FSExtra extra;
        };
        std::unordered_map<uint64_t, PendingSample> pending_samples;

        bool rolling_train = false;
        uint64_t samples_since_train = 0;
        uint64_t n_retrain = 0;

        uint64_t model_epoch = 0;
        uint64_t infer_cache_hit_cnt = 0;

        uint64_t pool_epoch = 0;
        std::vector<std::shared_ptr<DistanceModel>> infer_pool;

        double last_raw_score = 0;
        const uint64_t train_batch_size = 131072;
        std::mutex train_mtx;

        void after_train_sample() {
            if (!rolling_train) {
                return;
            }
            if (++samples_since_train < train_batch_size) {
                return;
            }
            samples_since_train = 0;
            std::lock_guard<std::mutex> tl(train_mtx);
            model_train.train();
            model_train.next();
            model_score.booster = model_train.booster;
            ++n_retrain;
            std::cout << "[rolling] retrain #" << n_retrain << " (batch " << train_batch_size << ")" << std::endl;
        }

    public:

        bool save_booster_file(const std::string &p) { return model_train.save_booster_to_file(p); }
        bool load_booster_file(const std::string &p) {
            bool ok = model_train.load_booster_from_file(p);
            if (ok) model_score.booster = model_train.booster;
            return ok;
        }

        std::vector<std::shared_ptr<DistanceMeta>>& get_metas() {
            return metas;
        }

    public:
        uint64_t mem_size() {
            uint64_t size = 0;
            for (int i = 0; i < metas.size(); ++i) {

                size += (metas[i] != nullptr ? metas[i]->mem_size() : sizeof(int));
            }

            size += model_score.mem_size_valid();

            size += pending_samples.size() * (sizeof(PendingSample) + 32 + sizeof(void *) * 5);
            std::queue<workloads::OpMeta> tmp_buf(req_buf);
            while (!tmp_buf.empty()) {
                auto op = tmp_buf.front();
                size += op.freq_mem_size();
                tmp_buf.pop();
            }
            size += sizeof(*this);
            return size;
        }

        ReqScoreDistance(int window_size, int key_range): window_size(window_size) {

            if ((config::learning_score_option & 64) != 0 && window_size > 0) {
                model_train.label_cap = 2 * static_cast<int64_t>(window_size);
            }

            rolling_train = (config::learning_score_option & 256) != 0;

            if ((config::learning_score_option & 2048) != 0) {
                inference_num_threads = 1;
            }
            metas.resize(key_range);
        }

        ~ReqScoreDistance() {
            auto size = mem_size();
            std::cout << "[DistanceBuff] memory consumption: " << cpputils::getSizeStr(size) << std::endl;
        }

        double freq_score(uint64_t key) {
            std::unique_lock<std::mutex> lock(buf_mtx);
            return freq_score_no_lock(key);
        }

        double freq_score_no_lock(uint64_t key) {
            if (metas[key] == nullptr) {
                return 0;
            }
            return static_cast<double>(metas[key]->n_within) / N_within;
        }

        void push_req(workloads::OpMeta op) {
            std::unique_lock<std::mutex> lock(buf_mtx);

            auto key = op.key;
            auto ts = op.ts;
            const bool fe = (config::lr_feature_set > 0);

            if (window_size > 0) {
                req_buf.push(op);
            }
            if (op.op == workloads::READ) {
                ++N_within;
                ++N_all;
                if (metas[key] == nullptr) {
                    metas[key] = std::make_shared<DistanceMeta>();
                    ++metas[key]->n_within;
                    ++metas[key]->n_all;
                    if (fe) {
                        metas[key]->last_any_ts = ts;
                        metas[key]->edc_ts = ts;
                    }
                } else {
                    ++metas[key]->n_within;
                    ++metas[key]->n_all;
                    auto new_delta = ts - metas[key]->last_ts;

                    if ((config::learning_score_option & 32) != 0 && train_data_flag) {
                        auto pit = pending_samples.find(key);
                        if (pit != pending_samples.end()) {
                            auto &snap = pit->second;
                            auto label_w = static_cast<int64_t>(ts) - static_cast<int64_t>(snap.ts);

                            bool outlier_w = (config::learning_score_option & 128) != 0
                                             && label_w < window_size
                                             && !snap.deltas.empty()
                                             && snap.deltas.front() > (uint32_t) window_size;
                            if (!outlier_w) {
                                if (model_train.push_back(metas[key], key, snap.freq, label_w,
                                                          true, snap.recency, snap.n_within, &snap.deltas,
                                                          (config::lr_feature_set >= 2) ? &snap.extra : nullptr)) {
                                    after_train_sample();
                                }
                            }
                            pending_samples.erase(pit);
                        }
                    }
                    if (train_data_flag) {

                        if ((config::learning_score_option & 32) == 0) {

                            bool outlier_r = (config::learning_score_option & 128) != 0
                                             && new_delta < (uint64_t) window_size
                                             && !metas[key]->deltas.empty()
                                             && metas[key]->deltas.front() > (uint32_t) window_size;
                            if (metas[key]->deltas.size() > 0 && ts >= config::init_train_pass) {
                                if (!outlier_r) {
                                FSExtra ex_r;
                                if (config::lr_feature_set >= 2) {
                                    compute_fs_features(metas[key].get(), ts, window_size, &ex_r);
                                }
                                if (model_train.push_back(metas[key], key, freq_score_no_lock(key), new_delta, true,
                                                         (config::lr_feature_set > 0) ? (int64_t) new_delta : 0,
                                                         metas[key]->n_within, nullptr,
                                                         (config::lr_feature_set >= 2) ? &ex_r : nullptr)) {
                                    after_train_sample();
                                }
                                }
                            }
                        }
                    }

                    if (metas[key]->n_within > 1) {
                        metas[key]->deltas.push_front(new_delta);
#ifdef ANOMALY

                        auto sum = metas[key]->sum += new_delta;
                        auto n = metas[key]->deltas.size();
                        auto ave = metas[key]->ave = sum / n;
                        double std_dev = 0;
                        for (const auto &item: metas[key]->deltas) {
                            std_dev += (item - ave) * (item - ave);
                        }

                        std_dev /= n;
                        metas[key]->std_dev = sqrt(std_dev);
#endif
                    }
                    if (fe) {

                        for (int j = 0; j < 4; ++j) {
                            double tau = (double) window_size * std::pow(4.0, j - 1);
                            if (tau < 1.0) tau = 1.0;
                            metas[key]->edc[j] = (float) ((double) metas[key]->edc[j]
                                                    * std::exp(-(double) new_delta / tau) + 1.0);
                        }
                        metas[key]->edc_ts = ts;
                    }
                }
                metas[key]->last_ts = ts;
            }

            if (op.op == workloads::UPDATE && (config::learning_score_option & 32) != 0
                && train_data_flag && ts >= config::init_train_pass
                && metas[key] != nullptr && !metas[key]->deltas.empty()
                && pending_samples.size() < 4000000) {
                PendingSample snap;
                snap.ts = ts;
                snap.recency = static_cast<int64_t>(ts) - static_cast<int64_t>(metas[key]->last_ts);
                snap.n_within = metas[key]->n_within;
                snap.freq = freq_score_no_lock(key);
                snap.deltas.assign(metas[key]->deltas.begin(), metas[key]->deltas.end());
                if (config::lr_feature_set >= 2) {
                    compute_fs_features(metas[key].get(), ts, window_size, &snap.extra);
                }
                pending_samples[key] = std::move(snap);
            }

            if (op.op == workloads::UPDATE && fe && metas[key] != nullptr) {

                ++metas[key]->n_write_all;
                metas[key]->has_write = true;
                metas[key]->last_write_ts = ts;
                ++metas[key]->n_write_within;
            }

            if (req_buf.size() > window_size) {
                auto out_op = req_buf.front();
                if (out_op.op == workloads::READ) {
                    if (metas[out_op.key]->deltas.size() > 0) {
#ifdef ANOMALY
                        metas[out_op.key]->sum -= metas[out_op.key]->deltas.back();
#endif
                        metas[out_op.key]->deltas.pop_back();
                    }
                    --metas[out_op.key]->n_within;
                    --N_within;
                } else if (out_op.op == workloads::UPDATE && (config::learning_score_option & 32) != 0 && train_data_flag) {

                    auto pit = pending_samples.find(out_op.key);
                    if (pit != pending_samples.end() && pit->second.ts == out_op.ts) {
                        if (model_train.push_back(metas[out_op.key], out_op.key, pit->second.freq,
                                                 2 * window_size, true, pit->second.recency, pit->second.n_within,
                                                 &pit->second.deltas,
                                                 (config::lr_feature_set >= 2) ? &pit->second.extra : nullptr)) {
                            after_train_sample();
                        }
                        pending_samples.erase(pit);
                    }
                }
                if (fe && out_op.op == workloads::UPDATE && metas[out_op.key] != nullptr) {
                    --metas[out_op.key]->n_write_within;
                }
                req_buf.pop();
            }

            if (ts != ts_) {
                std::cout << "ts: " << ts << std::endl;
                std::cout << "ts_: " << ts_ << std::endl;
                std::cout << "ts error in new_distance.h" << std::endl;
                exit(-1);
            }
            if (fe && metas[key] != nullptr) {
                metas[key]->last_any_ts = ts;
            }
            ++ts_;
        }

        Score get_score(uint64_t key) {

            std::unique_lock<std::mutex> lock(buf_mtx);

            if (metas[key] == nullptr || metas[key]->deltas.size() == 0) {
                auto score2 = ((config::learning_score_option & 1) != 0 ? freq_score_no_lock(key) : 0);
                score2 = ((config::learning_score_option == 4) ? (metas[key]->n_all / N_all) : score2);
                return {-2.0 * window_size, score2};
            }

            auto freq_score = freq_score_no_lock(key);

            if (model_score.booster == nullptr) {
                return {-2.0 * window_size, ((config::learning_score_option & 1) != 0 ? freq_score : 0)};
            }

            if ((config::learning_score_option & 8192) != 0) {
                if (metas[key]->pred_cache >= 0 && metas[key]->pred_n_all == metas[key]->n_all
                    && metas[key]->pred_epoch == model_epoch) {
                    ++infer_cache_hit_cnt;
                    auto sc2 = ((config::learning_score_option & 1) != 0 ? freq_score : 0);
                    return {-metas[key]->pred_cache, sc2};
                }
            }

            FSExtra ex;
            const bool fs_on = (config::lr_feature_set > 0);
            if (config::lr_feature_set >= 2) {
                compute_fs_features(metas[key].get(), ts_, window_size, &ex);
            }
            model_score.next();
            model_score.push_back(metas[key], key, freq_score, 0, false,
                                  (fs_on || (config::learning_score_option & 32) != 0)
                                      ? static_cast<int64_t>(ts_) - static_cast<int64_t>(metas[key]->last_ts) : 0,
                                  metas[key]->n_within, nullptr,
                                  (config::lr_feature_set >= 2) ? &ex : nullptr);
            lock.unlock();
            model_score.rank();
            auto score = model_score.scores[0];
            last_raw_score = score;
            score = expm1(score);

            if ((config::learning_score_option & 4096) != 0) {
                int nwv = metas[key]->n_within;
                double R = (nwv > 0) ? (double) window_size / nwv : 2.0 * window_size;
                score = std::sqrt(std::max(score, 1.0) * R);
            }
            auto score2 = ((config::learning_score_option & 1) != 0 ? freq_score : 0);
            if ((config::learning_score_option & 8192) != 0) {
                metas[key]->pred_cache = score;
                metas[key]->pred_n_all = metas[key]->n_all;
                metas[key]->pred_epoch = model_epoch;
            }
            return {-score, score2};
        }

        void get_scores(std::vector<uint64_t> &keys, std::vector<Score> &scores) {
            std::unique_lock<std::mutex> lock(buf_mtx);
            scores.resize(keys.size());
            std::vector<int> ids;
            FSExtra ex;
            const bool fs_on = (config::lr_feature_set > 0);
            model_score.next();
            for (int i = 0; i < keys.size(); ++i) {
                auto key = keys[i];
                if (metas[key] == nullptr || metas[key]->deltas.size() == 0) {
                    auto score2 = ((config::learning_score_option & 1) != 0 ? freq_score_no_lock(key) : 0);
                    scores[i] = {(-2.0 * window_size), score2};
                } else {
                    if ((config::learning_score_option & 8192) != 0 && metas[key]->pred_cache >= 0
                        && metas[key]->pred_n_all == metas[key]->n_all && metas[key]->pred_epoch == model_epoch) {
                        ++infer_cache_hit_cnt;
                        scores[i] = {-metas[key]->pred_cache, ((config::learning_score_option & 1) != 0 ? freq_score_no_lock(key) : 0)};
                        continue;
                    }
                    ids.push_back(i);
                    if (config::lr_feature_set >= 2) {
                        compute_fs_features(metas[key].get(), ts_, window_size, &ex);
                    }
                    model_score.push_back(metas[key], key, freq_score_no_lock(key), 0, false,
                                          (fs_on || (config::learning_score_option & 32) != 0)
                                              ? static_cast<int64_t>(ts_) - static_cast<int64_t>(metas[key]->last_ts) : 0,
                                          metas[key]->n_within, nullptr,
                                          (config::lr_feature_set >= 2) ? &ex : nullptr);
                    scores[i].score2 = ((config::learning_score_option & 1) != 0 ? freq_score_no_lock(key) : 0);
                }
            }

            model_score.inst_infer_threads = (config::infer_threads > 0) ? config::infer_threads : -1;
            int T = config::infer_threads;
            (void) T;
            if (false) {

                if ((int) infer_pool.size() != T - 1 || pool_epoch != model_epoch
                    || infer_pool[0]->booster == nullptr) {
                    std::cerr << "[pool] rebuilding T=" << T << " epoch=" << model_epoch << std::endl;
                    auto ms = model_train.save_booster_to_string();
                    std::cerr << "[pool] saved " << ms.size() << " bytes" << std::endl;
                    infer_pool.assign(T - 1, nullptr);
                    for (int t = 0; t < T - 1; ++t) {
                        infer_pool[t] = std::make_shared<DistanceModel>();
                        infer_pool[t]->inst_infer_threads = 1;
                        infer_pool[t]->load_booster_from_string(ms);
                        std::cerr << "[pool] w" << t << " loaded ok=" << (infer_pool[t]->booster != nullptr) << std::endl;
                    }
                    pool_epoch = model_epoch;
                }
                std::vector<std::vector<int>> chunks(T);
                for (int k = 0; k < (int) ids.size(); ++k) chunks[k % T].push_back(ids[k]);
                for (int t = 1; t < T; ++t) {
                    infer_pool[t - 1]->next();
                    for (int id : chunks[t]) {
                        auto key = keys[id];
                        if (config::lr_feature_set >= 2) {
                            compute_fs_features(metas[key].get(), ts_, window_size, &ex);
                        }
                        infer_pool[t - 1]->push_back(metas[key], key, freq_score_no_lock(key), 0, false,
                            (fs_on || (config::learning_score_option & 32) != 0) ? (int64_t)(ts_ - metas[key]->last_ts) : 0,
                            metas[key]->n_within, nullptr,
                            (config::lr_feature_set >= 2) ? &ex : nullptr);
                    }
                }
                lock.unlock();
                std::vector<std::thread> ths;
                for (int t = 1; t < T; ++t) ths.emplace_back([this, t] { infer_pool[t - 1]->rank(); });
                std::cerr << "[pool] workers launched=" << ths.size() << " rows0=" << chunks[0].size() << std::endl;
                model_score.rank();
                std::cerr << "[pool] main rank done" << std::endl;
                for (auto &th : ths) th.join();
                std::cerr << "[pool] all joined" << std::endl;
                lock.lock();
                for (int t = 0; t < T; ++t) {
                    auto *inst = (t == 0) ? &model_score : infer_pool[t - 1].get();
                    for (int k = 0; k < (int) chunks[t].size(); ++k) {
                        int id = chunks[t][k];
                        auto s1 = expm1(inst->scores[k]);
                        scores[id].score1 = -s1;
                        if ((config::learning_score_option & 8192) != 0) {
                            auto kk = keys[id];
                            metas[kk]->pred_cache = s1;
                            metas[kk]->pred_n_all = metas[kk]->n_all;
                            metas[kk]->pred_epoch = model_epoch;
                        }
                    }
                }
            } else {
                lock.unlock();
                model_score.rank();
                for (int i = 0; i < ids.size(); ++i) {
                    auto score1 = model_score.scores[i];
                    scores[ids[i]].score1 = -expm1(score1);
                    if ((config::learning_score_option & 8192) != 0) {
                        auto kk = keys[ids[i]];
                        metas[kk]->pred_cache = expm1(score1);
                        metas[kk]->pred_n_all = metas[kk]->n_all;
                        metas[kk]->pred_epoch = model_epoch;
                    }
                }
            }
        }

        std::tuple<double, double> get_score2(uint64_t key) {

            std::unique_lock<std::mutex> lock(buf_mtx);
            if (metas[key] == nullptr || metas[key]->deltas.size() == 0) {
                return std::make_tuple(0, -2 * window_size);
            }
            model_score.next();
            model_score.push_back(metas[key], key, freq_score_no_lock(key));
            model_score.rank();

             return std::make_tuple(freq_score(key), -(model_score.scores[0]));
        }

        void train_once() {
            if (rolling_train) {

                std::lock_guard<std::mutex> tl(train_mtx);
                if (model_train.labels.size() > 0) {
                    model_train.train();
                    model_train.next();
                    ++n_retrain;
                    std::cout << "[rolling] init-final retrain, samples=" << model_train.labels.size() << std::endl;
                }
                return;
            }
            model_train.train();
            ++model_epoch;
        }

        void train_mode() {
            train_data_flag = true;
        }
        void valid_mode() {

            train_data_flag = rolling_train ? true : false;
            model_score.booster = model_train.booster;
        }
    };
}

#endif
