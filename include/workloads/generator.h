
#ifndef INDEX_MAINTAIN_GENERATOR_H
#define INDEX_MAINTAIN_GENERATOR_H

#include <cmath>
#include <fstream>
#include <vector>
#include <sys/time.h>
#include <unistd.h>
#include <iostream>
#include <cassert>
#include <atomic>

#include "cpputils/random.h"
#include "cpputils/hash.h"

namespace workloads {
    extern cpputils::RandMode rand_mode;

    template<typename Value>
    class Generator {
    public:
        virtual Value Next(uint64_t offset = 0) = 0;
        virtual Value Last() = 0;
        virtual ~Generator() {}
    };

    class UniformGenerator: public Generator<uint64_t> {

    private:
        cpputils::RandIntUni<uint64_t> rand_uint_64;
        uint64_t last_int_{};
    public:
        UniformGenerator(uint64_t min, uint64_t max) : rand_uint_64(min, max, rand_mode) { Next(); }
        uint64_t Next(uint64_t offset = 0) override { return last_int_ = rand_uint_64(); }
        uint64_t Last() override { return last_int_; }
    };

    class CounterGenerator : public Generator<uint64_t> {
    private:
        std::atomic<uint64_t> counter_{};
    public:
        CounterGenerator(uint64_t start) : counter_(start) {}
        uint64_t Next(uint64_t offset = 0) override { return counter_.fetch_add(1); }
        uint64_t Last() override { return counter_.load() - 1; }
        void Set(uint64_t start) { counter_.store(start); }
    };

    class ZipfianGenerator : public Generator<uint64_t> {
    public:

        static double kZipfianConst;
        static const uint64_t kMaxNumItems = (UINT64_MAX >> 24);
        cpputils::RandReal<double> rand_double_u;
    private:
        uint64_t num_items_;
        uint64_t base_;

        double theta_, zeta_n_, eta_, alpha_, zeta_2_;
        uint64_t n_for_zeta_;
        uint64_t last_value_;

    public:
        ZipfianGenerator(uint64_t min, uint64_t max, double zipfian_const = kZipfianConst) :
                num_items_(max - min + 1), base_(min), theta_(zipfian_const),
                zeta_n_(0), n_for_zeta_(0), rand_double_u(0.0, 1.0, rand_mode) {
            std::cout << "Zipfian skew factor: " << kZipfianConst << std::endl;
            assert(num_items_ >= 2 && num_items_ < kMaxNumItems);
            zeta_2_ = Zeta(2, theta_);
            alpha_ = 1.0 / (1.0 - theta_);
            RaiseZeta(num_items_);
            eta_ = Eta();

            Next();
        }

        ZipfianGenerator(uint64_t num_items) :
                ZipfianGenerator(0, num_items - 1, kZipfianConst) {}

        uint64_t Next(uint64_t num_items, uint64_t offset) {
            assert(num_items >= 2 && num_items < kMaxNumItems);
            if (num_items > n_for_zeta_) {
                RaiseZeta(num_items);
                eta_ = Eta();
            }

            double u = rand_double_u();
            double uz = u * zeta_n_;

            if (uz < 1.0) {

                return last_value_ = (0 + offset) % num_items;
            }

            if (uz < 1.0 + std::pow(0.5, theta_)) {

                return last_value_ = (1 + offset) % num_items;
            }

            return last_value_ = (base_ + static_cast<uint64_t>(num_items * std::pow(eta_ * u - eta_ + 1, alpha_)) + offset) % num_items;
        }

        uint64_t Next(uint64_t offset = 0) { return Next(num_items_, offset); }

        uint64_t Last() { return last_value_; }

    private:

        void RaiseZeta(uint64_t num) {
            assert(num >= n_for_zeta_);
            zeta_n_ = Zeta(n_for_zeta_, num, theta_, zeta_n_);
            n_for_zeta_ = num;
        }

        double Eta() {
            return (1 - std::pow(2.0 / num_items_, 1 - theta_)) /
                   (1 - zeta_2_ / zeta_n_);
        }

        static double Zeta(uint64_t last_num, uint64_t cur_num,
                           double theta, double last_zeta) {
            double zeta = last_zeta;
            for (uint64_t i = last_num + 1; i <= cur_num; ++i) {
                zeta += 1 / std::pow(i, theta);
            }
            return zeta;
        }

        static double Zeta(uint64_t num, double theta) {
            return Zeta(0, num, theta, 0);
        }
    };

    class ScrambledZipfianGenerator : public Generator<uint64_t> {
    private:
        uint64_t base_;
        uint64_t num_items_;
        ZipfianGenerator generator_;
        uint64_t last_{};
    public:
        ScrambledZipfianGenerator(uint64_t min, uint64_t max,
                                  double zipfian_const = ZipfianGenerator::kZipfianConst) :
                base_(min), num_items_(max - min + 1),
                generator_(min, max, zipfian_const) {}

        explicit ScrambledZipfianGenerator(uint64_t num_items) :
                ScrambledZipfianGenerator(0, num_items - 1) {}

        uint64_t Next(uint64_t offset = 0) override {
            uint64_t value = generator_.Next(offset);

            value = base_ + cpputils::Hash(value) % num_items_;
            return last_ = value;
        }

        uint64_t Last() override { return last_; }
    };

    class SkewedLatestGenerator : public Generator<uint64_t> {
    private:
        CounterGenerator &basis_;
        ZipfianGenerator zipfian_;
        uint64_t last_;
    public:
        SkewedLatestGenerator(CounterGenerator &counter) :
                basis_(counter), zipfian_(basis_.Last()) {
            Next();
        }

        uint64_t Next(uint64_t offset = 0) {
            uint64_t max = basis_.Last();
            return last_ = max - zipfian_.Next(max);
        }

        uint64_t Last() { return last_; }
    };

    class ConstGenerator : public Generator<uint64_t> {
    public:
        ConstGenerator(int constant) : constant_(constant) {}

        uint64_t Next(uint64_t offset = 0) { return constant_; }

        uint64_t Last() { return constant_; }

    private:
        uint64_t constant_;
    };

    template<typename Value>
    class DiscreteGenerator : public Generator<Value> {
    private:
        std::vector<std::pair<Value, double>> values_;
        double sum_;
        Value last_;

        cpputils::RandReal<double> random_double;
    public:
        DiscreteGenerator() : sum_(0), random_double(0.0, 1.0, rand_mode) {}

        void AddValue(Value value, double weight) {
            if (values_.empty()) {
                last_ = value;
            }
            values_.push_back(std::make_pair(value, weight));
            sum_ += weight;
        }

        Value Next(uint64_t offset = 0) {
            double chooser = random_double();

            for (auto p: values_) {
                if (chooser < p.second / sum_) {
                    return last_ = p.first;
                }
                chooser -= p.second / sum_;
            }

            assert(false);
            return last_;
        }

        Value Last() { return last_; }
    };
}

#endif
