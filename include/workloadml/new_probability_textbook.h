
#ifndef INDEX_MAINTAIN_NEW_PROBABILITY_TEXTBOOK_H
#define INDEX_MAINTAIN_NEW_PROBABILITY_TEXTBOOK_H

#include <queue>
#include <iostream>
#include <memory>
#include "workloadml/new_base.h"
#include "cpputils/size.h"
#include "config.h"
#include <fstream>
#include <string>

#include <cmath>

namespace workloadml {
    class ReqScoreTextbook: public ReqScore {
        std::queue<workloads::OpMeta> req_buf;
        int window_size = 0;

        int* nums_within;
        std::mutex buf_mtx;
        uint64_t N_within = 0;
        int key_range_=0;

        bool ewma_on = false;
        double ewma_decay = 0.5;
        double* ew_cnt = nullptr;
        int64_t* ew_last = nullptr;
        double ewma_N = 0.0;
        int64_t ew_last_any = 0;
        uint64_t cur_ts_ = 0;

    public:
        uint64_t mem_size() {
            uint64_t size = 0;
            std::queue<workloads::OpMeta> tmp_buf(req_buf);
            while (!tmp_buf.empty()) {
                auto op = tmp_buf.front();

                size += op.freq_mem_size();
                tmp_buf.pop();
            }
            size += sizeof(int) * (key_range_+10);
            size += sizeof(*this);
            if (ewma_on) {
                size += (sizeof(double) + sizeof(int64_t)) * (key_range_ + 10);
            }
            return size;
        }

        ReqScoreTextbook(int window_size, int key_range): window_size(window_size), key_range_(key_range) {

            nums_within = new int[key_range+10];
            memset(nums_within, 0, sizeof(int) * (key_range+10));

            if (!config::freq_base_file.empty()) {
                if (window_size <= 0) {
                    load_base_freq(config::freq_base_file);
                } else {
                    std::cout << "[FrequencyBuff] freq_base_file ignored (score_window_size > 0): "
                              << config::freq_base_file << std::endl;
                }
            }

            if (config::freq_ewma_halflife > 0) {
                ewma_on = true;
                ewma_decay = std::pow(0.5, 1.0 / (double) config::freq_ewma_halflife);
                ew_cnt = new double[key_range + 10]();
                ew_last = new int64_t[key_range + 10]();
                if (window_size > 0) {
                    std::cout << "[FrequencyBuff] EWMA mode: hard window ignored (score_window_size="
                              << window_size << ")" << std::endl;
                }
                if (!config::freq_base_file.empty()) {
                    std::cout << "[FrequencyBuff] freq_base_file ignored (EWMA mode)" << std::endl;
                }
            }
        }

        void load_base_freq(const std::string &path) {
            if (key_range_ <= 0) {
                std::cout << "[FrequencyBuff] key_range_ <= 0, skip base freq loading" << std::endl;
                return;
            }
            std::ifstream in(path);
            if (!in) {
                std::cout << "[FrequencyBuff] cannot open base freq file: " << path << std::endl;
                return;
            }
            uint64_t lines = 0, sum = 0, folded = 0;
            uint64_t key = 0, cnt = 0;
            while (in >> key >> cnt) {
                uint64_t idx = key % static_cast<uint64_t>(key_range_);
                nums_within[idx] += static_cast<int>(cnt);
                N_within += cnt;
                sum += cnt;
                ++lines;
                if (idx != key) {
                    ++folded;
                }
            }
            std::cout << "[FrequencyBuff] base freq loaded: lines=" << lines
                      << ", sum=" << sum << ", folded_keys=" << folded
                      << ", N_within=" << N_within << std::endl;
        }
        ~ReqScoreTextbook() {
            auto size = mem_size();
            std::cout << "[FrequencyBuff] memory consumption: " << cpputils::getSizeStr(size) << std::endl;
            delete[] nums_within;
            delete[] ew_cnt;
            delete[] ew_last;
        }

        void push_req(workloads::OpMeta op) {
            std::unique_lock<std::mutex> lock(buf_mtx);
            cur_ts_ = op.ts;
            if (ewma_on) {
                if (op.op == workloads::READ) {
                    double d = std::pow(ewma_decay, (double) op.ts - (double) ew_last[op.key]);
                    ew_cnt[op.key] = ew_cnt[op.key] * d + 1.0;
                    ew_last[op.key] = (int64_t) op.ts;
                    double dg = std::pow(ewma_decay, (double) op.ts - (double) ew_last_any);
                    ewma_N = ewma_N * dg + 1.0;
                    ew_last_any = (int64_t) op.ts;
                }
                return;
            }
            if (window_size > 0) {
                req_buf.push(op);
            }
            if (op.op == workloads::READ) {
                ++nums_within[op.key];
                ++N_within;
            }
            if (window_size > 0 && req_buf.size() > window_size) {
                auto out_op = req_buf.front();
                if (out_op.op == workloads::READ) {
                    --nums_within[out_op.key];
                    --N_within;
                }
                req_buf.pop();
            }
        }

        double freq_score(uint64_t key) {
            std::unique_lock<std::mutex> lock(buf_mtx);
            if (ewma_on) {
                double d = std::pow(ewma_decay, (double) cur_ts_ - (double) ew_last[key]);
                double dg = std::pow(ewma_decay, (double) cur_ts_ - (double) ew_last_any);
                double n = ewma_N * dg;
                if (n <= 0) return 0.0;
                return (ew_cnt[key] * d) / n;
            }
            return static_cast<double>(nums_within[key]) / N_within;
        }

        Score get_score(uint64_t key) {
            auto score = freq_score(key);
            return {score};
        }

        void get_scores(std::vector<uint64_t> &keys, std::vector<Score> &scores) {
            std::unique_lock<std::mutex> lock(buf_mtx);
            scores.resize(keys.size());
            if (ewma_on) {
                double dg = std::pow(ewma_decay, (double) cur_ts_ - (double) ew_last_any);
                double n = ewma_N * dg;
                for (int i = 0; i < keys.size(); ++i) {
                    if (n <= 0) { scores[i].score1 = 0.0; continue; }
                    double d = std::pow(ewma_decay, (double) cur_ts_ - (double) ew_last[keys[i]]);
                    scores[i].score1 = (ew_cnt[keys[i]] * d) / n;
                }
                return;
            }
            for (int i = 0; i < keys.size(); ++i) {
                scores[i].score1 = static_cast<double>(nums_within[keys[i]]) / N_within;
            }
        }
    };
}

#endif
