
#ifndef YCSB_WORKLOAD_DYN
#define YCSB_WORKLOAD_DYN

#include "workloads/workloads.h"
#include <list>

namespace workloads {
    namespace ycsb {
        class YCSBWorkloadDyn {
        public:
            DiscreteGenerator<Operation> op_chooser;

            size_t key_range;
            int switch_batch_, status = 0;
            uint64_t cnt=0;
            Generator<uint64_t> *read_key_chooser_, *update_key_chooser_;

            YCSBWorkloadDyn(const uint64_t key_size, double read_p, double update_p, int switch_batch) {
                key_range = key_size;
                switch_batch_ = switch_batch;

                op_chooser.AddValue(READ, read_p);
                op_chooser.AddValue(UPDATE, update_p);

                read_key_chooser_ = new ZipfianGenerator(key_range);
                update_key_chooser_ = new UniformGenerator(0, key_range - 1);
            }
            Operation NextOperation() { return op_chooser.Next(); }
            uint64_t NextReadTransactionKey(uint64_t offset = 0) {
                uint64_t key_num;
                key_num = read_key_chooser_->Next(offset);
                return key_num;
            }

            uint64_t NextUpdateTransactionKey() {
                uint64_t key_num;
                key_num = update_key_chooser_->Next();
                return key_num;
            }

            void init_status() {
                cnt = 0;
                status = 0;
            }

            OpMeta NextOpMeta(int offset=0);
        };

        int value_len_min = 4, value_len_max = 8;
        inline OpMeta YCSBWorkloadDyn::NextOpMeta(int offset) {
            OpMeta meta;
            static cpputils::RandString rand_value(value_len_min, value_len_max);
            meta.op = NextOperation();
            ++cnt;
            if (cnt % switch_batch_ == 0) {
                status ^= 1;
            }
            if (status == 0) {
                if (meta.op == READ) {
                    meta.key = NextReadTransactionKey();
                } else if (meta.op == UPDATE) {
                    meta.key = NextUpdateTransactionKey();
                    meta.value = rand_value();
                }
            } else if (status == 1) {
                if (meta.op == READ) {
                    meta.key = key_range - 1 - NextReadTransactionKey();
                } else if (meta.op == UPDATE) {
                    meta.key = NextUpdateTransactionKey();
                    meta.value = rand_value();
                }
            }
            return meta;
        }
    }
}

#endif
