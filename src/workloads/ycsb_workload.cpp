
#include "workloads/ycsb_workload.h"
#include "cpputils/hash.h"
#include "cpputils/random.h"

using namespace std;

namespace workloads {
    namespace ycsb {

        shared_ptr<YCSBWorkload> YCSBWorkload::CreatePredefinedWorkload(string&& workload_name) {
            shared_ptr<YCSBWorkload> workload{new YCSBWorkload()};
            if (workload_name == "A") {
                workload->Init(0.5, 0.5, 0, 0, 0, "zipfian");
            } else if (workload_name == "B") {
                workload->Init(0.95, 0.05, 0, 0, 0, "zipfian");
            } else if (workload_name == "C") {
                workload->Init(1, 0, 0, 0, 0, "zipfian");
            } else if (workload_name == "D") {
                workload->Init(0.95, 0, 0.05, 0, 0, "latest");
            } else if (workload_name == "E") {
                workload->Init(0, 0, 0.05, 0.95, 0, "zipfian");
            } else {
                std::cerr << "Wrong Type!\n";
                return nullptr;
            }
            return workload;
        }

        shared_ptr<YCSBWorkload> YCSBWorkload::CreateWorkload(double read_p, double update_p, double insert_p, uint64_t rw_offset) {
            shared_ptr<YCSBWorkload> workload{new YCSBWorkload()};
            workload->Init(read_p, update_p, insert_p, 0, 0, "zipfian");
            workload->rw_offset = rw_offset;
            return workload;
        }

        bool use_pre_gen_values;
        int value_len_min = 4, value_len_max = 8;
        vector<string>&& long_string_values = pre_gen_values();
        vector<string> pre_gen_values(int num) {
            vector<string> values;
            values.reserve(num);
            cpputils::RandString rand_value(value_len_min, value_len_max);
            for (int i = 0; i < num; ++i) {
                values.emplace_back(std::move(rand_value()));

            }
            cout << values.size() << endl;
            return values;
        }

        OpMeta YCSBWorkload::NextOpMeta() {
            OpMeta meta;
            meta.op = NextOperation();

            static cpputils::RandString rand_value(value_len_min, value_len_max);
            static cpputils::RandIntUni<int> rand_idx(0, long_string_values.size()-1);
            int value_idx;
            switch (meta.op) {
                case UPDATE:
                case INSERT:
                    meta.table_name = NextTable();
                    meta.key = NextUInt64TransactionKey(rw_offset);
                    if (use_pre_gen_values) {
                        value_idx = rand_idx();
                        meta.value = long_string_values[value_idx];
                    } else {
                        meta.value = std::move(rand_value());
                    }

                    break;
                case READ:
                    meta.table_name = NextTable();
                    meta.key = NextUInt64TransactionKey();
                    break;

                default:
                    std::cout << "Not Supported Operation!\n";
                    std::cout << "Not Supposed to Reach Here!\n";
                    break;
            }
            return meta;
        }

        inline std::string YCSBWorkload::NextSequenceKey() {
            uint64_t key_num = key_generator_->Next();
            return BuildKeyName(key_num);
        }

        inline uint64_t YCSBWorkload::NextUInt64SequenceKey() {
            return key_generator_->Next();
        }

        inline std::string YCSBWorkload::NextTransactionKey() {
            uint64_t key_num;
            do {
                key_num = key_chooser_->Next();
            } while (key_num > insert_key_sequence_.Last());
            return BuildKeyName(key_num);
        }

        inline uint64_t YCSBWorkload::NextUInt64TransactionKey(uint64_t offset) {
            uint64_t key_num;
            do {
                key_num = key_chooser_->Next(offset);
            } while (key_num > insert_key_sequence_.Last());

            return key_num;
        }

        inline std::string YCSBWorkload::BuildKeyName(uint64_t key_num) {
            if (!ordered_inserts_) {
                key_num = cpputils::Hash(key_num);
            }
            return std::string("user").append(std::to_string(key_num));
        }

        inline std::string YCSBWorkload::NextFieldName() {
            return std::string("field").append(std::to_string(field_chooser_->Next()));
        }

        void
        YCSBWorkload::Init(double read_proportion, double update_proportion, double insert_proportion, double scan_proportion,
                           double readmodifywrite_proportion, const std::string &request_dist, const std::string &table_name) {
            table_name_ = table_name;
            field_count_ = 10;
            field_len_generator_.reset(new ConstGenerator(10));

            record_count_ = workloads::init_record_count;
            scan_len_chooser_.reset(new UniformGenerator(1, 1000));
            int insert_start = 0;

            read_all_fields_ = true;
            write_all_fields_ = true;
            ordered_inserts_ = false;

            key_generator_.reset(new CounterGenerator(insert_start));

            if (read_proportion > 0) {
                op_chooser_.AddValue(READ, read_proportion);
            }
            if (update_proportion > 0) {
                op_chooser_.AddValue(UPDATE, update_proportion);
            }
            if (insert_proportion > 0) {
                op_chooser_.AddValue(INSERT, insert_proportion);
            }
            if (scan_proportion > 0) {
                op_chooser_.AddValue(SCAN, scan_proportion);
            }
            if (readmodifywrite_proportion > 0) {
                op_chooser_.AddValue(READMODIFYWRITE, readmodifywrite_proportion);
            }

            insert_key_sequence_.Set(record_count_);

            if (request_dist == "uniform") {
                key_chooser_.reset(new UniformGenerator(0, record_count_ - 1));
            } else if (request_dist == "zipfian") {

                int op_count = 1000;
                int new_keys = (int) (op_count * insert_proportion * 2);

                key_chooser_.reset(new ScrambledZipfianGenerator(record_count_ + new_keys));

            } else if (request_dist == "latest") {
                key_chooser_.reset(new SkewedLatestGenerator(insert_key_sequence_));
            }

            field_chooser_.reset(new UniformGenerator(0, field_count_ - 1));
        }

        void YCSBWorkload::BuildValues(std::vector<KVPair> &values) {
            for (int i = 0; i < field_count_; ++i) {
                KVPair pair;
                pair.first.append("field").append(std::to_string(i));
                pair.second.append(field_len_generator_->Next(), cpputils::RandomPrintChar());
                values.push_back(pair);
            }
        }

        void YCSBWorkload::BuildUpdate(std::vector<KVPair> &update) {
            KVPair pair;
            pair.first.append(NextFieldName());
            pair.second.append(field_len_generator_->Next(), cpputils::RandomPrintChar());
            update.push_back(pair);
        }
    }
}