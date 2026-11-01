
#ifndef INDEX_MAINTAIN_NEW_DISTANCE_TEXTBOOK_H
#define INDEX_MAINTAIN_NEW_DISTANCE_TEXTBOOK_H

#include "workloadml/new_base.h"
#include <queue>
#include <iostream>
#include <memory>
#include <mutex>

namespace workloadml {
    class ReqScoreDistanceTextbook: public ReqScore {
        std::queue<workloads::OpMeta> req_buf;
        std::vector<std::queue<uint64_t>> dis;
        int window_size = 0;
        std::mutex buf_mtx;
        uint64_t ts = 0;

    public:
        uint64_t mem_size() {
            uint64_t size = 0;
            std::queue<workloads::OpMeta> tmp_buf(req_buf);
            while (!tmp_buf.empty()) {
                auto op = tmp_buf.front();
                size += op.mem_size();
                tmp_buf.pop();
            }
            size += sizeof(*this);
            return size;
        }

        ReqScoreDistanceTextbook(int window_size, int key_range): window_size(window_size) {
            dis.resize(key_range);
        }

        void push_req(workloads::OpMeta op) {
            std::unique_lock<std::mutex> lock(buf_mtx);
            if (window_size > 0) {
                req_buf.push(op);
            }
            if (op.op == workloads::READ) {
                dis[op.key].push(op.ts);
            }
            if (req_buf.size() > window_size) {
                auto out_op = req_buf.front();
                if (out_op.op == workloads::READ) {
                    dis[out_op.key].pop();
                }
                req_buf.pop();
            }
            ++ts;
        }

        void push_req_null() {
            std::unique_lock<std::mutex> lock(buf_mtx);
            auto out_op = req_buf.front();
            if (out_op.op == workloads::READ) {
                dis[out_op.key].pop();
            }
            req_buf.pop();
            ++ts;
        }

        Score get_score(uint64_t key) {
            std::unique_lock<std::mutex> lock(buf_mtx);
            auto next_ts = dis[key].front();
            return {static_cast<double>(-(next_ts - (ts - window_size)))};
        }

        void get_scores(std::vector<uint64_t> &keys, std::vector<Score> &scores) {

        }
    };
}

#endif
