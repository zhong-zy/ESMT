
#ifndef INDEX_MAINTAIN_RANDOM_H
#define INDEX_MAINTAIN_RANDOM_H

#include <random>
#include <sys/time.h>
#include <cstring>
#include <typeinfo>
#include <sstream>

namespace cpputils {
    enum RandMode {
        RandSeed,
        SetSeed,
        DefaultSeed,
    };
    extern int setted_seed;
    class Rand {
    public:
        std::default_random_engine re;
        std::mt19937_64 re64;
        long sd;

        static auto get_rand_seed() {
            timeval t0;
            gettimeofday(&t0, nullptr);
            return t0.tv_sec * 1000000 + t0.tv_usec;
        }

        void seed(int s) {
            re.seed(s);
            re64.seed(s);
            sd = s;
        }
        void rand_seed() {
            auto seed = Rand::get_rand_seed();
            re.seed(seed);
            re64.seed(seed);
            sd = seed;
        }
        explicit Rand(RandMode seed_mode = DefaultSeed) {
            switch (seed_mode) {
                case RandSeed:
                    this->rand_seed();
                    break;
                case SetSeed:
                    this->seed(setted_seed);
                    break;
                case DefaultSeed:
                    break;
            }
        }
    };

    template<class type>
    class RandIntUni: public Rand {
    private:
        std::uniform_int_distribution<type> dist;
    public:
        RandIntUni(type low, type high, RandMode seed_mode = DefaultSeed) : dist(low, high), Rand(seed_mode) {}
        int operator()() {
            if (typeid(type) == typeid(uint64_t) || typeid(type) == typeid(int64_t)) {
                return dist(re64);
            } else {
                return dist(re);
            }
        }
    };

    template<class type>
    class RandReal: public Rand {
    private:
        std::uniform_real_distribution<type> dist;
    public:
        RandReal(type low, type high, RandMode seed_mode = DefaultSeed): dist(low, high), Rand(seed_mode) {}
        type operator()() { return dist(re); }
    };

    inline char RandomPrintChar() {
        int ret = rand() % 94 + 33;
        return (ret == ',' || ret == '"') ? RandomPrintChar() : ret;
    }

    static constexpr char CCH[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

    class RandString: public RandIntUni<int> {
    private:
        size_t CCH_len;
        std::uniform_int_distribution<int> dist_s;
    public:
        RandString(int low, int high, RandMode seed_mode = DefaultSeed): RandIntUni<int>(low, high, seed_mode), CCH_len(strlen(CCH)), dist_s(0, CCH_len - 1) {}

        std::string operator()() {
            auto len = RandIntUni::operator()();

            std::stringstream ss;
            for (int i = 0; i < len; ++i) {
                int index = dist_s(re64);

                ss << CCH[index];
            }

            return ss.str();
        }
    };
}

#endif
