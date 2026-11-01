
#ifndef INDEX_MAINTAIN_SMALLBANK_WORKLOAD_H
#define INDEX_MAINTAIN_SMALLBANK_WORKLOAD_H

#include "workloads/workloads.h"
#include <list>

namespace workloads {
    namespace ycsb {
        class YCSBWorkloadNew {
        public:
            DiscreteGenerator<Operation> op_chooser;

            size_t key_range;
            uint64_t cnt=0;
            Generator<uint64_t> *key_chooser_;
            int value_len_min = 4, value_len_max = 8;
            vector<string> long_string_values;

            YCSBWorkloadNew(const uint64_t key_size, double read_p, double update_p, int size_min, int size_max, bool scramble=true) {
                key_range = key_size;

                op_chooser.AddValue(READ, read_p);
                op_chooser.AddValue(UPDATE, update_p);
                value_len_min = size_min;
                value_len_max = size_max;

                if (scramble) {
                    key_chooser_ = new ScrambledZipfianGenerator(key_range);
                } else {
                    key_chooser_ = new ZipfianGenerator(key_range);
                }

                int value_num = 100;
                cpputils::RandString rand_value(value_len_min, value_len_max);
                for (int i = 0; i < value_num; ++i) {
                    long_string_values.emplace_back(std::move(rand_value()));
                }
            }
            Operation NextOperation() { return op_chooser.Next(); }
            uint64_t NextUInt64TransactionKey(uint64_t offset = 0) {
                uint64_t key_num;
                key_num = key_chooser_->Next(offset);
                return key_num;
            }

            OpMeta NextOpMeta(int offset=0);
        };

        inline OpMeta YCSBWorkloadNew::NextOpMeta(int offset) {
            OpMeta meta;
            static cpputils::RandString rand_value(value_len_min, value_len_max);
            static cpputils::RandIntUni<int> rand_idx(0, long_string_values.size()-1);
            meta.op = NextOperation();
            if (meta.op == READ) {
                meta.key = NextUInt64TransactionKey(offset);
            } else if (meta.op == UPDATE) {
                meta.key = NextUInt64TransactionKey(offset);

                auto value_idx = rand_idx();
                meta.value = long_string_values[value_idx];
            }
            return meta;
        }
    }
}

#endif
