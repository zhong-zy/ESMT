
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
            int switch_batch_, status = 0;
            uint64_t cnt=0;
            Generator<uint64_t> *key_chooser_;

            YCSBWorkloadNew(const uint64_t key_size, double read_p, double update_p, int switch_batch) {
                key_range = key_size;
                switch_batch_ = switch_batch;

                op_chooser.AddValue(READ, read_p);
                op_chooser.AddValue(UPDATE, update_p);

                key_chooser_ = new ZipfianGenerator(key_range);
            }
            Operation NextOperation() { return op_chooser.Next(); }
            uint64_t NextUInt64TransactionKey(uint64_t offset = 0) {
                uint64_t key_num;
                key_num = key_chooser_->Next(offset);
                return key_num;
            }

            OpMeta NextOpMeta(int offset=0);
        };

        int value_len_min = 4, value_len_max = 8;
        inline OpMeta YCSBWorkloadNew::NextOpMeta(int offset) {
            OpMeta meta;
            static cpputils::RandString rand_value(value_len_min, value_len_max);
            meta.op = NextOperation();
            ++cnt;
            if (cnt % switch_batch_ == 0) {
                status ^= 1;
            }
            if (status == 0) {
                if (meta.op == READ) {
                    meta.key = NextUInt64TransactionKey();
                } else if (meta.op == UPDATE) {

                    meta.key = key_range - 1 - NextUInt64TransactionKey();
                    meta.value = rand_value();
                }
            } else if (status == 1) {
                if (meta.op == READ) {
                    meta.key = key_range - 1 - NextUInt64TransactionKey();
                } else if (meta.op == UPDATE) {

                    meta.key = NextUInt64TransactionKey();
                    meta.value = rand_value();
                }
            }
            return meta;
        }
    }
}

#endif
