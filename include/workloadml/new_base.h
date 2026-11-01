
#ifndef INDEX_MAINTAIN_NEW_BASE_H
#define INDEX_MAINTAIN_NEW_BASE_H

#include "workloads/workloads.h"
#include "workloadml/score.h"
#include <mutex>
#include <memory>

namespace workloadml {
    class ReqScore {
    public:
        virtual ~ReqScore() = default;
        virtual void push_req(workloads::OpMeta op) = 0;
        virtual Score get_score(uint64_t key) = 0;

        virtual void get_scores(std::vector<uint64_t> &keys, std::vector<Score> &scores)=0;
        virtual uint64_t mem_size()=0;
    };
}

#endif
