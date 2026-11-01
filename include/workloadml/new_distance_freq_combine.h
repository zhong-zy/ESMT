
#ifndef INDEX_MAINTAIN_NEW_DISTANCE_FREQ_COMBINE_H
#define INDEX_MAINTAIN_NEW_DISTANCE_FREQ_COMBINE_H

#include "workloadml/new_distance.h"
#include "workloadml/new_probability_textbook.h"

namespace workloadml {
    class ReqScoreDistanceFreq: ReqScore {
        ReqScoreDistance *dis_model;
        ReqScoreTextbook *freq_model;

        int window_size = 0;
        mutex buf_mtx;
        int thread = 5000;
    public:
        ReqScoreDistanceFreq(int window_size, int key_range): window_size(window_size) {
            dis_model = new ReqScoreDistance(thread, key_range);
            freq_model = new ReqScoreTextbook(window_size, key_range);
        }

        void push_req(workloads::OpMeta op) {

        }
    };
}

#endif
