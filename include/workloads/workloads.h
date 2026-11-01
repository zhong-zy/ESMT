
#ifndef INDEX_MAINTAIN_WORKLOADS_H
#define INDEX_MAINTAIN_WORKLOADS_H

#include <chrono>
#include <string>
#include "workloads/generator.h"
#include <memory>
#include "workloadml/score.h"

typedef uint64_t KeyType;

namespace workloads {
    typedef std::pair<std::string, std::string> KVPair;
    enum Operation {
        INSERT,
        READ,
        UPDATE,
        SCAN,
        READMODIFYWRITE,
        UPDATE_ADS,
        READ_HASH
    };

    class OpMeta {
    public:
        Operation op;
        std::string table_name = "";
        KeyType key = 0;
        std::string value = "";
        int next = 0;
        int next_read_ts = 0;

        int64_t ts = 0;

        workloadml::Score priority = 0;

        double fs[16] = {0};
        int64_t snap_recency = 0;
        int snap_n_within = 0;
        double snap_freq = 0;
        uint32_t snap_delta[8] = {0};
        int snap_ndelta = 0;
        bool snap_valid = false;
        double valid_time = 0;
        std::chrono::steady_clock::time_point proof_latency_st, proof_latency_ed;
        std::chrono::steady_clock::time_point pi_latency_st, pi_latency_ed;

        uint64_t mem_size() {
            return value.size() * sizeof(char)
            + table_name.size() * sizeof(char)
            + sizeof(*this);
        }

        uint64_t freq_mem_size() {
            return sizeof(KeyType) + sizeof(key);
        }

        friend std::ostream& operator<<(std::ostream &os, OpMeta& meta) {
            os << "Meta {" << std::endl;
            os << "op:" << meta.op << std::endl;
            os << "table_name:" << meta.table_name << std::endl;
            os << "key:" << meta.key << std::endl;
            os << "value:" << meta.value << std::endl;
            os << "}" << std::endl;
            return os;
        }
    };

    extern uint32_t init_record_count;
}

#endif