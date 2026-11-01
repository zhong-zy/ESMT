
#ifndef INDEX_MAINTAIN_WORKLOAD_SIMULATOR_H
#define INDEX_MAINTAIN_WORKLOAD_SIMULATOR_H

#include "dbmaintainer/mbpt/mbpt.h"
#include "dbmaintainer/db_mbpt.h"

namespace workloadsim {
    extern ADS_MOD ads_mod;

    class WorkloadSimulator {
    private:
        int prepare_size = 1048576;
        int op_cnt = 500000;
        string db_path = "./db";

        shared_ptr<DB_MBPT> db;

        void Init();
    public:
        int producer_num = 1;

        explicit WorkloadSimulator(int prepare_size=1048576, string db_path="./db", int op_cnt = 500000): prepare_size(prepare_size), db_path(std::move(db_path)), op_cnt(op_cnt) {
            Init();
        }

        void mt_prepare();

        void start_predifined_workload(string&& workload_name);

        void start_proportion_workload(double read_p, double update_p, double insert_p, uint64_t rw_offset);

        void start_file_workload(std::string workload_path);
    };
}

#endif
