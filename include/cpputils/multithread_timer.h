
#ifndef INDEX_MAINTAIN_MULTITHREAD_TIMER_H
#define INDEX_MAINTAIN_MULTITHREAD_TIMER_H

#include <mutex>

#include "timer.h"

namespace cpputils {
    class MultiThreadTimer: public Timer{
        std::mutex mtx;
        int thread_cnt = 0;

    public:
        void start() {
            std::unique_lock<std::mutex> lock(mtx);
            if (thread_cnt == 0) {
                Timer::start();
            }
            thread_cnt++;
        }

        void end(std::string&& msg = "") {
            std::unique_lock<std::mutex> lock(mtx);
            thread_cnt--;
            if (thread_cnt == 0) {
                Timer::end();
                show_duration(msg);
            }
        }
    };
}

#endif
