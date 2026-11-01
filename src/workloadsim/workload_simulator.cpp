
#include <thread>

#include "dbmaintainer/mbpt/mbpt.h"
#include "workloadsim/workload_simulator.h"
#include "workloadsim/processor.h"

using namespace std;

namespace workloadsim {
    ADS_MOD ads_mod;
    void WorkloadSimulator::Init() {

        db = make_shared<DB_MBPT_DAII>(db_path, ads_mod);
    }

    void WorkloadSimulator::mt_prepare() {

        cout << "Preparing init data" << endl;

        vector<KeyType> preparedKeys;
        preparedKeys.reserve(prepare_size);
        for (int i = 0; i < prepare_size; i++) {
            preparedKeys.push_back(i);
        }

        cout << "finished" << endl;

    }

    void WorkloadSimulator::start_predifined_workload(string&& workload_name) {
        cout << "start predefined workload" << endl;

        vector<thread> thread_pool;
        thread_pool.reserve(producer_num);
        for (int i = 0; i < producer_num; ++i) {
            thread_pool.emplace_back(workloadsim::predefined_workload_producer, workload_name);
        }
        cout << "finished start " << producer_num << " workload generator(s)" << endl;

        thread consumer{workloadsim::workload_consumer, ref(db), 1};
        cout << "finished start DB." << endl;

        for (auto &item: thread_pool) {
            item.join();
        }
        cout << "workload generator(s) join." << endl;
        consumer.join();
        cout << "consumer join." << endl;
    }

    void WorkloadSimulator::start_proportion_workload(double read_p, double update_p, double insert_p, uint64_t rw_offset) {
        cout << "start proportion workload" << endl;

        vector<thread> thread_pool;
        thread_pool.reserve(producer_num);
        for (int i = 0; i < producer_num; ++i) {
            thread_pool.emplace_back(workloadsim::proportion_workload_producer, read_p, update_p, insert_p, rw_offset);
        }
        cout << "finished start " << producer_num << " workload generator(s)" << endl;

        thread consumer{workloadsim::workload_consumer, ref(db), 1};
        cout << "finished start DB." << endl;

        for (auto &item: thread_pool) {
            item.join();
        }
        cout << "workload generator(s) join." << endl;
        consumer.join();
        cout << "consumer join." << endl;
    }

    void WorkloadSimulator::start_file_workload(std::string workload_path) {
        cout << "start proportion workload" << endl;
        thread producer{workloadsim::file_workload_producer, workload_path};
        cout << "finished start workload generator" << endl;
        vector<thread> committers;
        db->process_always();
        for (int i = 1; i <= config::committer_cnt; ++i) {
            committers.emplace_back(workloadsim::workload_consumer, ref(db), i);
        }

        cout << "finished start DB." << endl;
        producer.join();
        cout << "workload generator(s) join." << endl;
        for (auto &committer: committers) {
            committer.join();
        }
        db->finish_commit = steady_clock::now();
        cout << "committers join" << endl;
        db->process_join();
        cout << "db->process_join" << endl;

    }
}