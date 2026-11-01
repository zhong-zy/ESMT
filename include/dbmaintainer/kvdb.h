
#ifndef INDEX_MAINTAIN_KVDB_H
#define INDEX_MAINTAIN_KVDB_H

#include <chrono>

#include "workloads/ycsb_workload.h"
#include "dbmaintainer/mbpt/merkle.h"

#include "cpputils/parallel.h"

using namespace std::chrono;

namespace workloadsim {

    class DBResult {
    public:
        std::string data;

        DBResult(DBResult&) = default;

        DBResult(DBResult&&) = default;

    };

    class KVDB {
    public:
        virtual ~KVDB() = default;
        time_point<steady_clock> finish_commit;
        time_point<steady_clock> commit_st;

        time_point<steady_clock> latency_anchor;
        uint64_t latency_op_cnt = 0;
    protected:
        uint64_t read_cnt = 0, write_cnt = 0, update_cnt = 0, r_dirty_cnt = 0, batch_loss_cnt = 0;
        uint64_t read_cnt_last = 0, write_cnt_last = 0, update_cnt_last = 0;
        duration<int64_t> verbose_interval = 1s;
        time_point<steady_clock> start, data_finished_time;

        std::thread monitor_thread;
        std::thread learning_thread;
        bool monitor_thread_stop = false;
    public:

        virtual void submit_op(workloads::OpMeta& op) = 0;

        virtual void process_always() = 0;
        virtual void process_join() = 0;

        virtual void update(workloads::OpMeta& op) = 0;

        virtual DBResult read(workloads::OpMeta& op) = 0;
    };

}

#endif
