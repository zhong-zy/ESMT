#ifndef INDEX_MAINTAIN_NEW_DISTANCE_DELTA0_H
#define INDEX_MAINTAIN_NEW_DISTANCE_DELTA0_H

#include "workloadml/new_base.h"
#include "config.h"
#include <LightGBM/c_api.h>
#include <iostream>
#include <vector>
#include <queue>
#include <deque>
#include "cpputils/size.h"

namespace workloadml {
    class DistanceDelta0Meta {
    public:
        std::deque<uint32_t> deltas;
        uint64_t last_ts;
        int n_within = 0;
        uint64_t n_all = 0;

#ifdef ANOMALY
        uint64_t sum=0;
        double ave=0;
        double std_dev=0;
#endif
        uint64_t mem_size() {
            if (n_within == 0) {
                return sizeof(int);
            }

            return sizeof(uint32_t) + sizeof(*this);
        }
    };

    extern const char *training_params;

    class ReqScoreDistanceDelta0: public ReqScore {
    public:
        bool train_data_flag = false;
        std::vector<std::shared_ptr<DistanceDelta0Meta>> metas;

        std::queue<workloads::OpMeta> req_buf;
        int window_size = 0;

        std::mutex buf_mtx;
        uint64_t ts_ = 0;
        uint64_t N_within = 0;
        uint64_t N_all = 0;

    public:
        std::vector<std::shared_ptr<DistanceDelta0Meta>>& get_metas() {
            return metas;
        }

    public:
        uint64_t mem_size() {
            uint64_t size = 0;
            for (int i = 0; i < metas.size(); ++i) {

                size += (metas[i] != nullptr ? metas[i]->mem_size() : sizeof(int));
            }

            std::queue<workloads::OpMeta> tmp_buf(req_buf);
            while (!tmp_buf.empty()) {
                auto op = tmp_buf.front();
                size += op.freq_mem_size();
                tmp_buf.pop();
            }
            size += sizeof(*this);
            return size;
        }

        ReqScoreDistanceDelta0(int window_size, int key_range): window_size(window_size) {
            metas.resize(key_range);
        }

        ~ReqScoreDistanceDelta0() {
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

            if (window_size > 0) {
                req_buf.push(op);
            }
            if (op.op == workloads::READ) {
                ++N_within;
                ++N_all;
                if (metas[key] == nullptr) {
                    metas[key] = std::make_shared<DistanceDelta0Meta>();
                    ++metas[key]->n_within;
                    ++metas[key]->n_all;
                } else {
                    ++metas[key]->n_within;
                    ++metas[key]->n_all;
                    auto new_delta = ts - metas[key]->last_ts;

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
                }
                metas[key]->last_ts = ts;
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
                }
                req_buf.pop();
            }

            if (ts != ts_) {
                std::cout << "ts: " << ts << std::endl;
                std::cout << "ts_: " << ts_ << std::endl;
                std::cout << "ts error in new_distance.h" << std::endl;
                exit(-1);
            }
            ++ts_;
        }

        Score get_score(uint64_t key) {

            std::unique_lock<std::mutex> lock(buf_mtx);

            if (metas[key] == nullptr || metas[key]->deltas.size() == 0) {
                return {-2.0 * window_size, 0};
            }

            auto score = metas[key]->deltas.front();
            return {-1.0 *score, 0};
        }

        void get_scores(std::vector<uint64_t> &keys, std::vector<Score> &scores) {
            std::unique_lock<std::mutex> lock(buf_mtx);
            scores.resize(keys.size());
            for (int i = 0; i < keys.size(); ++i) {
                auto key = keys[i];
                if (metas[key] == nullptr || metas[key]->deltas.size() == 0) {
                    scores[i] = {(-2.0 * window_size), 0};
                } else {
                    scores[i].score1 = metas[key]->deltas.front();
                    scores[i].score2 = 0;
                }
            }
        }

        std::tuple<double, double> get_score2(uint64_t key) {

        }

    };
}

#endif
