
#include <map>

#include <thread>

#include "workloadsim/processor.h"
#include "cpputils/timer.h"
#include "cpputils/sync_ostream.h"

#include "dbmaintainer/db_mbpt.h"
#include "cpputils/parallel.h"
#include "cpputils/multithread_timer.h"

using namespace workloads::ycsb;
using namespace std;

namespace workloadsim {

    cpputils::Generator<workloads::OpMeta> file_buf_pc;

    cpputils::MultiThreadTimer produce_timer;

    bool pre_gen_workload = false;

    void predefined_workload_producer(string&& workload_name) {
        produce_timer.start();

        auto workload = YCSBWorkload::CreatePredefinedWorkload(std::move(workload_name));
        while (true) {
            if (!file_buf_pc.need_produce()) {
                break;
            }

            auto meta = workload->NextOpMeta();
            file_buf_pc.produce(meta);
        }

        produce_timer.end("Producer");
    }

    void file_workload_producer(std::string filePath) {
        uint64_t ts = 0;
        produce_timer.start();
        std::ifstream file(filePath);
        std::hash<uint64_t> hasher;
        while (true) {
            if (!file_buf_pc.need_produce()) {
                break;
            }

            if (config::process_cnt && (ts >= config::init_training_cnt + config::process_cnt)) {
                break;
            }

            workloads::OpMeta meta;
            int op;
            file >> meta.key >> op >> meta.next >> meta.next_read_ts;

            meta.key %= workloads::init_record_count;

            meta.op = static_cast<workloads::Operation>(op);
            meta.ts = ts;
            if (meta.op == UPDATE) {
                file >> meta.value;
            }
            file_buf_pc.produce(meta);
            ++ts;
        }
        file_buf_pc.finish_produce();
        produce_timer.end("File Producer ");
        cout << "produce cnt: " << ts << endl;
    }

    void proportion_workload_producer(double read_p, double update_p, double insert_p, uint64_t rw_offset) {
        produce_timer.start();

        std::string filePath = "../../proportion_workloads/workload_" + to_string(read_p) + "_" + to_string(update_p) + "_" + to_string(insert_p) + "_" +
                               to_string(workloadsim::file_buf_pc.max_ops) + "_" + to_string(rw_offset);
        std::ifstream file(filePath);
        uint64_t ts = 0;
        if (pre_gen_workload && file.good()) {
            while (true) {
                if (!file_buf_pc.need_produce()) {
                    break;
                }

                workloads::OpMeta meta;
                int op;
                file >> meta.key >> op;
                meta.op = static_cast<workloads::Operation>(op);
                if (meta.op == UPDATE) {
                    file >> meta.value;
                }
                meta.ts = ts;
                file_buf_pc.produce(meta);
                ++ts;
            }
            file.close();
        } else {
            file.close();
            ofstream  ofile;
            if (pre_gen_workload) {
                ofile.open(filePath, ios::app);
            }
            cout << "generate workload" << endl;
            auto workload = YCSBWorkload::CreateWorkload(read_p, update_p, insert_p, rw_offset);
            while (true) {
                if (!file_buf_pc.need_produce()) {
                    break;
                }

                auto meta = workload->NextOpMeta();
                if (pre_gen_workload) {
                    ofile << meta.key << "\t" << meta.op << "\t" << meta.value << endl;
                }
                meta.ts = ts;
                file_buf_pc.produce(meta);
                ++ts;
            }
            if (pre_gen_workload) {
                ofile.close();
            }
        }

        produce_timer.end("Producer");
    }

    void workload_consumer(const shared_ptr<DB_MBPT>& db, int consumer_id) {

        int counter = 0;
        int i = 0;
        while(true) {

            unique_lock<mutex> lock(db->committer_mtx);
            auto [need_con, op] = file_buf_pc.try_consume();
            if (!need_con) {
                break;
            }
            db->submit_op(op);

            ++db->commit_cnt;
            ++counter;
            auto delay = (db->commit_cnt > config::init_training_cnt ? config::tx_delay : 0);
            lock.unlock();
            this_thread::sleep_for(chrono::microseconds(delay));

        }
        cout << "Consumer " << consumer_id << " end, submit " << counter << " operations" << endl;

    }

    void consumer_show() {
        map<uint64_t, int> counter;
        int all_counter = 0;
        while(true) {
            if (!file_buf_pc.need_consume()) {
                break;
            }

            auto&& m = file_buf_pc.consume();
            counter[m.key]++;
            all_counter++;
        }
        this_thread::sleep_for(chrono::seconds {1});
        cpputils::sos.lock();
        cout << "counter:" << endl;
        cpputils::sos.unlock();
        for(auto [key, count]: counter) {
            cpputils::sos.lock();
            cout << key << "\t" << count << endl;
            cpputils::sos.unlock();
        }
        cpputils::sos.lock();
        cout << "all_counter:" << all_counter << endl;
        cpputils::sos.unlock();
    }

    void consumer_show_offset() {
        multiset<uint64_t> counter1, counter2, result;
        int all_counter = 0, w_counter = 0, r_counter = 0;
        while(true) {
            if (!file_buf_pc.need_consume()) {
                break;
            }

            auto&& m = file_buf_pc.consume();
            if (m.op == UPDATE) {
                counter1.insert(m.key);
                ++w_counter;
            } else if (m.op == READ) {
                counter2.insert(m.key);
                ++r_counter;
            }
            all_counter++;
        }
        set_intersection(counter1.begin(), counter1.end(), counter2.begin(), counter2.end(), inserter(result, result.begin()));
        this_thread::sleep_for(chrono::seconds {1});
        cpputils::sos.lock();
        cout << "counter:" << endl;
        cpputils::sos.unlock();
        cout << "intersection: " << result.size() << endl;
        cout << "reads: " << r_counter << endl;
        cout << "writes: " << w_counter << endl;

        cpputils::sos.lock();
        cout << "all_counter:" << all_counter << endl;
        cpputils::sos.unlock();
    }
}