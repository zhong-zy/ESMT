#ifndef PK_STATS_H
#define PK_STATS_H

#include <vector>
#include <atomic>
#include <cstdint>
#include <cmath>
#include <cstddef>

namespace pkstats {

    extern std::vector<char>      g_cleaned;
    extern std::atomic<uint64_t>  g_wasted;
    extern std::atomic<uint64_t>  g_marked;
    extern std::vector<uint32_t>  g_nr, g_nw;
    extern std::vector<int64_t>   g_ts_any, g_ts_read, g_ts_write;
    extern std::atomic<uint64_t>  g_bg_cnt, g_bg_gcnt, g_bg_gsum, g_bg_le3, g_bg_le15, g_bg_le63, g_bg_q_sum;

    inline std::vector<char> &cleaned_flag() { return g_cleaned; }
    inline uint64_t wasted_cnt() { return g_wasted.load(); }
    inline void add_wasted() { ++g_wasted; }
    inline uint64_t marked_cnt() { return g_marked.load(); }
    inline size_t flag_size() { return g_cleaned.size(); }
    inline std::vector<uint32_t> &nr() { return g_nr; }
    inline std::vector<uint32_t> &nw() { return g_nw; }
    inline std::vector<int64_t> &ts_any() { return g_ts_any; }
    inline std::vector<int64_t> &ts_read() { return g_ts_read; }
    inline std::vector<int64_t> &ts_write() { return g_ts_write; }

    inline void ensure(size_t n) { if (g_cleaned.size() < n) g_cleaned.assign(n, 0); }
    inline void ensure_pk(size_t n) {
        if (g_nr.size() < n) {
            g_nr.assign(n, 0);
            g_nw.assign(n, 0);
            g_ts_any.assign(n, -1);
            g_ts_read.assign(n, -1);
            g_ts_write.assign(n, -1);
        }
    }
    inline void mark_cleaned(uint64_t k) { ++g_marked; if (k < g_cleaned.size()) g_cleaned[k] = 1; }
    inline bool take_cleaned(uint64_t k) {
        if (k < g_cleaned.size() && g_cleaned[k]) { g_cleaned[k] = 0; return true; }
        return false;
    }
    inline void clear_cleaned(uint64_t k) { if (k < g_cleaned.size()) g_cleaned[k] = 0; }

    inline double phat_freq(uint64_t k, double alpha) {
        if (k >= g_nr.size()) return 0.5;
        double r = (double) g_nr[k], w = (double) g_nw[k];
        return (r + alpha) / (r + w + 2.0 * alpha);
    }
    inline void on_read(uint64_t k, int64_t ts) {
        if (k < g_nr.size()) { ++g_nr[k]; g_ts_read[k] = ts; g_ts_any[k] = ts; }
    }

    inline void on_write(uint64_t k, int64_t ts) {
        if (k < g_nw.size()) { ++g_nw[k]; g_ts_any[k] = ts; g_ts_write[k] = ts; }
    }

    inline double phat_lr(uint64_t k, int64_t now) {
        if (k >= g_nr.size()) return 0.5;
        static const double W0 = 0.453666, W1 = -0.072877, W2 = -0.025033,
                           W3 = -0.025031, W4 = -0.012853, W5 = 0.037236;
        double r = (double) g_nr[k], w = (double) g_nw[k], n = r + w;
        double ratio = (r + 2.0) / (n + 4.0);
        double rec = (g_ts_any[k] >= 0) ? std::log1p((double) (now - g_ts_any[k])) : -1.0;
        double sr = (g_ts_read[k] >= 0) ? std::log1p((double) (now - g_ts_read[k])) : -1.0;
        double sw = (g_ts_write[k] >= 0) ? std::log1p((double) (now - g_ts_write[k])) : -1.0;
        double z = W0 + W1 * ratio + W2 * rec + W3 * sr + W4 * sw + W5 * std::log1p(n);
        return 1.0 / (1.0 + std::exp(-z));
    }

    inline void note_blocked(uint64_t k, int64_t ts, int64_t qdepth) {
        ++g_bg_cnt;
        g_bg_q_sum += (uint64_t) (qdepth > 0 ? qdepth : 0);
        if (k < g_ts_write.size()) {
            int64_t w = g_ts_write[k];
            if (w >= 0 && ts >= w) {
                uint64_t gap = (uint64_t) (ts - w);
                ++g_bg_gcnt;
                g_bg_gsum += gap;
                if (gap <= 3)  ++g_bg_le3;
                if (gap <= 15) ++g_bg_le15;
                if (gap <= 63) ++g_bg_le63;
            }
        }
    }
    inline double bg_mean() { uint64_t c = g_bg_gcnt.load(); return c ? (double) g_bg_gsum.load() / (double) c : 0.0; }
    inline double bg_le_frac(int t) {
        uint64_t c = g_bg_gcnt.load();
        if (!c) return 0.0;
        uint64_t v = (t <= 3) ? g_bg_le3.load() : ((t <= 15) ? g_bg_le15.load() : g_bg_le63.load());
        return 100.0 * (double) v / (double) c;
    }
    inline double bg_q_mean() { uint64_t c = g_bg_cnt.load(); return c ? (double) g_bg_q_sum.load() / (double) c : 0.0; }
}
#endif
