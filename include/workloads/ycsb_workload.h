
#ifndef INDEX_MAINTAIN_YCSB_WORKLOAD_H
#define INDEX_MAINTAIN_YCSB_WORKLOAD_H

#include <memory>
#include "workloads/workloads.h"
#include "workloads/generator.h"
#include <string>

namespace workloads {
    namespace ycsb {

        extern bool use_pre_gen_values;
        extern std::vector<std::string>&& long_string_values;
        std::vector<std::string> pre_gen_values(int num = 100);

        class YCSBWorkload {
        public:

            virtual void
            Init(double read_proportion, double update_proportion, double insert_proportion, double scan_proportion,
                 double readmodifywrite_proportion, const std::string &request_dist,
                 const std::string &table_name = "ycsb");

            static std::shared_ptr<YCSBWorkload> CreatePredefinedWorkload(std::string&& workload_name);
            static std::shared_ptr<YCSBWorkload> CreateWorkload(double read_p, double update_p, double insert_p, uint64_t rw_offset);
            OpMeta NextOpMeta();

            virtual void BuildValues(std::vector<KVPair> &values);

            virtual void BuildUpdate(std::vector<KVPair> &update);

            virtual std::string NextTable() { return table_name_; }

            virtual std::string NextSequenceKey();
            virtual std::string NextTransactionKey();

            virtual uint64_t NextUInt64SequenceKey();
            virtual uint64_t NextUInt64TransactionKey(uint64_t offset = 0);
            virtual Operation NextOperation() { return op_chooser_.Next(); }

            virtual std::string NextFieldName();

            virtual size_t NextScanLength() { return scan_len_chooser_->Next(); }

            bool read_all_fields() const { return read_all_fields_; }

            bool write_all_fields() const { return write_all_fields_; }

            YCSBWorkload() :
                    field_count_(0), read_all_fields_(false), write_all_fields_(false),
                    field_len_generator_(nullptr), key_generator_(nullptr), key_chooser_(nullptr),
                    field_chooser_(nullptr), scan_len_chooser_(nullptr), insert_key_sequence_(3),
                    ordered_inserts_(true), record_count_(0) {
            }

            virtual ~YCSBWorkload() = default;

        public:
            std::string BuildKeyName(uint64_t key_num);

            std::string table_name_;
            int field_count_;
            bool read_all_fields_;
            bool write_all_fields_;
            std::unique_ptr<Generator<uint64_t>> field_len_generator_;
            std::unique_ptr<Generator<uint64_t>> key_generator_;
            DiscreteGenerator<Operation> op_chooser_;
            std::unique_ptr<Generator<uint64_t>> key_chooser_;
            std::unique_ptr<Generator<uint64_t>> field_chooser_;
            std::unique_ptr<Generator<uint64_t>> scan_len_chooser_;
            CounterGenerator insert_key_sequence_;
            bool ordered_inserts_;
            size_t record_count_;

            uint64_t rw_offset;
        };
    }
}

#endif
