
#ifndef INDEX_MAINTAIN_PROCESSOR_H
#define INDEX_MAINTAIN_PROCESSOR_H

#include <condition_variable>
#include <queue>
#include <mutex>

#include "workloads/ycsb_workload.h"
#include "dbmaintainer/mbpt/mbpt.h"
#include "dbmaintainer/db_mbpt.h"

using namespace workloads::ycsb;
namespace workloadsim {
    extern cpputils::Generator<workloads::OpMeta> file_buf_pc;

    extern bool pre_gen_workload;
    void predefined_workload_producer(string&& workload_name);
    void file_workload_producer(std::string filePath);
    void proportion_workload_producer(double read_p, double update_p, double insert_p, uint64_t rw_offset);
    void workload_consumer(const shared_ptr<DB_MBPT>& db, int consumer_id);
    void consumer_show();
    void consumer_show_offset();
}

#endif
