
#ifndef INDEX_MAINTAIN_TIMER_H
#define INDEX_MAINTAIN_TIMER_H

#include "sys/time.h"
#include <sys/types.h>
#include <string>

namespace cpputils {
    int64_t getTimestamp();

    class Timer {
    protected:
        u_int64_t startTime;
        u_int64_t endTime;
    public:
        void start() {
            struct timeval tv;
            gettimeofday(&tv, nullptr);
            u_int64_t time = (u_int64_t)tv.tv_sec * 1000 + (u_int64_t)tv.tv_usec / 1000;
            this->startTime = time;
        }
        void end() {
            struct timeval tv;
            gettimeofday(&tv, nullptr);
            u_int64_t time = (u_int64_t)tv.tv_sec * 1000 + (u_int64_t)tv.tv_usec / 1000;
            this->endTime = time;
        }
        u_int64_t duration() {
            return this->endTime - this->startTime;
        }
        void show_duration(const std::string& msg = "", std::ostream& os = std::cout) {
            os << msg << "Duration: " << duration() << " ms" << std::endl;
        }
    };
}

#endif
