
#ifndef INDEX_MAINTAIN_SMALLBANK_WORKLOAD_H
#define INDEX_MAINTAIN_SMALLBANK_WORKLOAD_H

#include "workloads/workloads.h"
#include <list>

namespace workloads {
    namespace smallbank {
        enum Transaction {
            Balance,
            DepositCheckiong,
            TransactSaving,
            Amalgamate,
            WriteCheck,
        };

        enum Table {
            Account = -1,
            Saving = 0,
            Checking,
        };

        class SmallBankWorkload {
        public:
            DiscreteGenerator<Transaction> trans_chooser;

            size_t record_count_, total_key_cnt, table_num = 3;
            Generator<uint64_t> *key_chooser_;
            std::vector<uint64_t> table_start;

            SmallBankWorkload(const int key_size) {
                record_count_ = key_size;

                total_key_cnt = record_count_ * 2;

                table_start.resize(table_num);

                table_start[Table::Saving] = Table::Saving * record_count_;
                table_start[Table::Checking] = Table::Checking * record_count_;

                trans_chooser.AddValue(Balance, 0.2);
                trans_chooser.AddValue(DepositCheckiong, 0.2);
                trans_chooser.AddValue(TransactSaving, 0.2);
                trans_chooser.AddValue(Amalgamate, 0.2);
                trans_chooser.AddValue(WriteCheck, 0.2);

                key_chooser_ = new ZipfianGenerator(record_count_);
            }
            Transaction NextTransaction() { return trans_chooser.Next(); }
            uint64_t NextUInt64TransactionKey(uint64_t offset = 0) {
                uint64_t key_num;
                key_num = key_chooser_->Next(offset);
                return key_num;
            }

            std::vector<OpMeta> NextTransMetas(int offset);
        };

        inline std::vector<OpMeta> SmallBankWorkload::NextTransMetas(int offset) {
            std::vector<OpMeta> ops;
            static cpputils::RandIntUni<int> rand_value(0, INT32_MAX);
            auto trans = NextTransaction();
            int i=0;
            if (trans == Balance) {

                ops.resize(2);
                auto key = NextUInt64TransactionKey(offset);

                ops[i].key = key + table_start[Saving];
                ops[i].op = READ;
                ++i;
                ops[i].key = key + table_start[Checking];
                ops[i].op = READ;
            } else if (trans == DepositCheckiong) {

                ops.resize(2);
                auto key = NextUInt64TransactionKey(offset);
                auto value = std::to_string(rand_value());

                ops[i].key = key + table_start[Checking];
                ops[i].op = READ;
                ++i;
                ops[i].key = key + table_start[Checking];
                ops[i].op = UPDATE;
                ops[i].value = value;
            } else if (trans == TransactSaving) {

                ops.resize(2);
                auto key = NextUInt64TransactionKey(offset);
                auto value = std::to_string(rand_value());

                ops[i].key = key + table_start[Saving];
                ops[i].op = READ;
                ++i;
                ops[i].key = key + table_start[Saving];
                ops[i].op = UPDATE;
                ops[i].value = value;
            } else if (trans == Amalgamate) {

                ops.resize(6);
                auto key1 = NextUInt64TransactionKey(offset);
                uint64_t key2;
                do {
                    key2 = NextUInt64TransactionKey(offset);
                } while (key2 == key1);

                ops[i].key = key1 + table_start[Saving];
                ops[i].op = READ;
                ++i;
                ops[i].key = key1 + table_start[Saving];
                ops[i].op = UPDATE;
                ops[i].value = std::to_string(0);
                ++i;
                ops[i].key = key1 + table_start[Checking];
                ops[i].op = READ;
                ++i;
                ops[i].key = key1 + table_start[Checking];
                ops[i].op = UPDATE;
                ops[i].value = std::to_string(0);
                ++i;
                ops[i].key = key2 + table_start[Checking];
                ops[i].op = READ;
                ++i;
                ops[i].key = key2 + table_start[Checking];
                ops[i].op = UPDATE;
                ops[i].value = std::to_string(rand_value());
            } else if (trans == WriteCheck) {

                ops.resize(3);
                auto key = NextUInt64TransactionKey(offset);
                auto value = std::to_string(rand_value());

                ops[i].key = key + table_start[Saving];
                ops[i].op = READ;
                ++i;
                ops[i].key = key + table_start[Checking];
                ops[i].op = READ;
                ++i;
                ops[i].key = key + table_start[Checking];
                ops[i].op = UPDATE;
                ops[i].value = value;
            }
            return ops;
        }
    }
}

#endif
