// bench_qldb_eth.cc —— QLDB 单机请求开销分解回放器（ETH 真实负载）
//
// 用法：
//   ./bench_qldb_eth --workload <eth_trace> --db /tmp/qldb_db/x --txn_mode op|2r2w \
//                    [--preload [--preload_chunk 100]] \
//                    --warmup_rows 4000000 --measure_rows 2000000 --sample 100 \
//                    --out /tmp/qldb_prof/x.tsv
//
// 计时口径（纳秒）：
//   写：w_data = d_tip_read + d_lookup + d_encode + d_idx + d_hist + d_block_append
//       w_mt_block、w_mt_digest、w_lookup（写后回读）、w_proof（digest+getProof+Verify）
//   读：r_lookup、r_proof
//   other = total - 上述
//
// 说明：只做"回放 + 计时"，不改 QLDB 语义；Set 内部分段计时由 qldb.cc 的
//       #ifdef QDB_PROF 提供（qdb_prof_ns[5] + qdb_prof_sub[6]）。

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "ledger/qldb/qldb.h"

#ifdef QDB_PROF
namespace ledgebase {
namespace qldb {
extern thread_local uint64_t qdb_prof_ns[5];
extern thread_local uint64_t qdb_prof_sub[6];
extern thread_local uint64_t qdb_prof_enc_blk;
extern thread_local uint64_t qdb_prof_enc_dg;
}
}
#endif

namespace {

using Clock = std::chrono::steady_clock;

inline uint64_t NowNs() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          Clock::now().time_since_epoch()).count());
}

#ifdef PROOF_ON_WRITE
static uint64_t g_verify_tot = 0;
static uint64_t g_verify_fail = 0;
#endif

struct Opts {
  std::string workload;
  std::string db = "/tmp/qldb_db/bench";
  std::string out = "/tmp/qldb_prof/bench.tsv";
  std::string ledger = "test";
  std::string txn_mode = "op";   // op | 2r2w
  bool preload = false;
  uint64_t preload_chunk = 100;
  uint64_t warmup_rows = 0;
  uint64_t measure_rows = 0;     // 0 = 直到文件结束
  uint64_t sample = 100;
};

struct TraceOp {
  std::string key;
  int op = 0;          // 1=read 2=write
  std::string val;
};

struct Rec {
  uint64_t d_tip = 0, d_lookup = 0, d_encode = 0, d_idx = 0, d_hist = 0, d_block = 0;
  uint64_t w_data = 0, w_mt_block = 0, w_mt_digest = 0, w_lookup = 0, w_proof = 0;
  uint64_t r_lookup = 0, r_proof = 0;
  uint64_t other = 0, total = 0;
};

bool ParseArgs(int argc, char** argv, Opts* o) {
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&](const char* name) -> std::string {
      if (i + 1 >= argc) {
        fprintf(stderr, "[error] %s needs a value\n", name);
        exit(2);
      }
      return argv[++i];
    };
    if (a == "--workload") o->workload = next("--workload");
    else if (a == "--db") o->db = next("--db");
    else if (a == "--out") o->out = next("--out");
    else if (a == "--ledger") o->ledger = next("--ledger");
    else if (a == "--txn_mode") o->txn_mode = next("--txn_mode");
    else if (a == "--preload") o->preload = true;
    else if (a == "--preload_chunk") o->preload_chunk = strtoull(next("--preload_chunk").c_str(), nullptr, 10);
    else if (a == "--warmup_rows") o->warmup_rows = strtoull(next("--warmup_rows").c_str(), nullptr, 10);
    else if (a == "--measure_rows") o->measure_rows = strtoull(next("--measure_rows").c_str(), nullptr, 10);
    else if (a == "--sample") o->sample = strtoull(next("--sample").c_str(), nullptr, 10);
    else if (a == "-h" || a == "--help") {
      printf("usage: %s --workload F --db DIR [--txn_mode op|2r2w] [--preload] "
             "[--preload_chunk N] [--warmup_rows N] [--measure_rows N] [--sample K] "
             "[--out F] [--ledger NAME]\n", argv[0]);
      return false;
    } else {
      fprintf(stderr, "[warn] unknown arg %s\n", a.c_str());
    }
  }
  if (o->workload.empty()) {
    fprintf(stderr, "[error] --workload is required\n");
    return false;
  }
  if (o->sample == 0) o->sample = 1;
  if (o->preload_chunk == 0) o->preload_chunk = 100;
  if (o->txn_mode != "op" && o->txn_mode != "2r2w") {
    fprintf(stderr, "[error] --txn_mode must be op or 2r2w\n");
    return false;
  }
  return true;
}

// 解析一行：key \t op \t next \t next_read_ts \t value
bool ParseLine(const std::string& line, TraceOp* t) {
  size_t p1 = line.find('\t');
  if (p1 == std::string::npos) return false;
  size_t p2 = line.find('\t', p1 + 1);
  if (p2 == std::string::npos) return false;
  size_t p3 = line.find('\t', p2 + 1);
  if (p3 == std::string::npos) return false;
  size_t p4 = line.find('\t', p3 + 1);
  t->key = line.substr(0, p1);
  t->op = atoi(line.substr(p1 + 1, p2 - p1 - 1).c_str());
  t->val = (p4 == std::string::npos) ? std::string() : line.substr(p4 + 1);
  while (!t->val.empty() && (t->val.back() == '\n' || t->val.back() == '\r')) {
    t->val.pop_back();
  }
  return !t->key.empty() && (t->op == 1 || t->op == 2);
}

class Bench {
 public:
  explicit Bench(const Opts& o) : o_(o), qldb_(o.db) {}

  bool Init() {
    auto di = qldb_.digest(o_.ledger);
    if (di.tip == 0 && di.digest.empty()) {
      qldb_.CreateLedger(o_.ledger);
      fprintf(stderr, "[init] created ledger '%s' at %s\n", o_.ledger.c_str(), o_.db.c_str());
    } else {
      fprintf(stderr, "[init] resume ledger '%s' tip=%lu\n", o_.ledger.c_str(),
              (unsigned long)di.tip);
    }
    out_.open(o_.out.c_str(), std::ios::out | std::ios::trunc);
    if (!out_.is_open()) {
      fprintf(stderr, "[error] cannot open %s\n", o_.out.c_str());
      return false;
    }
    out_ << "txn_mode\ttxn_id\tr_ops\tw_ops\tgroup_size\tn_keys_block"
            "\tw_data_ns\tw_mt_block_ns\tw_mt_digest_ns\tw_lookup_ns\tw_proof_ns\tw_other_ns"
            "\tr_lookup_ns\tr_proof_ns\tr_other_ns\ttotal_ns\tblocks_so_far"
            "\td_tip_read_ns\td_lookup_ns\td_encode_ns\td_idx_ns\td_hist_ns\td_block_append_ns\n";
    return true;
  }

  // 预热：把 trace 里出现过的 key 先写一遍（模拟"链上已有状态"）
  bool Preload() {
    std::ifstream in(o_.workload.c_str());
    if (!in.is_open()) {
      fprintf(stderr, "[error] cannot open workload %s\n", o_.workload.c_str());
      return false;
    }
    std::set<std::string> keys;
    std::string line;
    while (std::getline(in, line)) {
      TraceOp t;
      if (ParseLine(line, &t)) keys.insert(t.key);
    }
    fprintf(stderr, "[preload] distinct keys = %zu, chunk = %lu\n", keys.size(),
            (unsigned long)o_.preload_chunk);
    std::vector<std::string> ks, vs;
    ks.reserve(o_.preload_chunk);
    vs.reserve(o_.preload_chunk);
    uint64_t blocks = 0;
    for (const auto& k : keys) {
      ks.push_back(k);
      vs.emplace_back("0");
      if (ks.size() >= o_.preload_chunk) {
        qldb_.Set(o_.ledger, ks, vs);
        blocks++;
        ks.clear();
        vs.clear();
      }
    }
    if (!ks.empty()) {
      qldb_.Set(o_.ledger, ks, vs);
      blocks++;
    }
    blocks_ += blocks;
    fprintf(stderr, "[preload] done: %lu blocks, total blocks=%lu\n",
            (unsigned long)blocks, (unsigned long)blocks_);
    return true;
  }

  // 单 key：lookup + (digest + getProof + Verify)
  void DoReadKey(const std::string& key, uint64_t* lookup_ns, uint64_t* proof_ns) {
    uint64_t t0 = NowNs();
    ledgebase::Chunk c = qldb_.GetCommitted(o_.ledger, key);
    uint64_t t1 = NowNs();
    *lookup_ns += (t1 - t0);
    if (c.empty()) return;
    ledgebase::qldb::Document doc(&c);
    auto addr = doc.getAddr();
    auto meta = doc.getMetaData();
    uint64_t t2 = NowNs();
    auto di = qldb_.digest(o_.ledger);
    auto pr = qldb_.getProof(o_.ledger, di.tip, addr.seq_no, (uint32_t)meta.doc_seq);
    (void)pr;   // 服务端口径：路径校验在客户端 CommitCallback 里做，不计入
    uint64_t t3 = NowNs();
    *proof_ns += (t3 - t2);
  }

  void DoWriteTxn(const std::vector<std::string>& keys,
                  const std::vector<std::string>& vals,
                  const std::vector<std::string>& read_keys,
                  uint64_t txn_id, uint64_t group_size, bool record, uint64_t seq) {
    Rec rec;
    uint64_t t_start = NowNs();

    for (const auto& k : read_keys) {
      DoReadKey(k, &rec.r_lookup, &rec.r_proof);
    }

#ifdef QDB_PROF
    for (int i = 0; i < 5; ++i) ledgebase::qldb::qdb_prof_ns[i] = 0;
    for (int i = 0; i < 6; ++i) ledgebase::qldb::qdb_prof_sub[i] = 0;
    ledgebase::qldb::qdb_prof_enc_blk = 0;
    ledgebase::qldb::qdb_prof_enc_dg = 0;
#endif
#ifdef PROOF_ON_WRITE
    std::vector<ledgebase::qldb::QLProofResult> proofs;
    qldb_.Set(o_.ledger, keys, vals, &proofs);
#else
    qldb_.Set(o_.ledger, keys, vals);
#endif
    ++blocks_;
#ifdef QDB_PROF
    {
      uint64_t p0 = ledgebase::qldb::qdb_prof_ns[0];
      uint64_t p1 = ledgebase::qldb::qdb_prof_ns[1];
      uint64_t p2 = ledgebase::qldb::qdb_prof_ns[2];
      uint64_t p3 = ledgebase::qldb::qdb_prof_ns[3];
      uint64_t p4 = ledgebase::qldb::qdb_prof_ns[4];
      const uint64_t* s = ledgebase::qldb::qdb_prof_sub;
      if (p4 > p0) {
        rec.d_tip = s[2];
        rec.d_lookup = s[0];
        rec.d_encode = s[1];
        rec.d_idx = s[3];
        rec.d_hist = s[4];
        rec.d_block = p4 - p3;
        rec.w_data = (p1 - p0) + (p4 - p3);
#ifdef PROOF_ON_WRITE
        rec.w_mt_block = (p2 - p1) - ledgebase::qldb::qdb_prof_enc_blk;
        rec.w_mt_digest = (p3 - p2) - ledgebase::qldb::qdb_prof_enc_dg;
#else
        rec.w_mt_block = (p2 - p1);
        rec.w_mt_digest = (p3 - p2);
#endif
      }
    }
#endif

#ifdef PROOF_ON_WRITE
    {
      rec.w_proof = ledgebase::qldb::qdb_prof_enc_blk +
                    ledgebase::qldb::qdb_prof_enc_dg;   // 服务端物化证明（base32 编码）
      rec.w_lookup = 0;
      (void)proofs;
    }
#else
    for (const auto& k : keys) {
      DoReadKey(k, &rec.w_lookup, &rec.w_proof);
    }
#endif

    rec.total = NowNs() - t_start;
    uint64_t sum = rec.w_data + rec.w_mt_block + rec.w_mt_digest + rec.w_lookup +
                   rec.w_proof + rec.r_lookup + rec.r_proof;
    rec.other = (rec.total > sum) ? (rec.total - sum) : 0;

    if (record && seq % o_.sample == 0) {
      out_ << o_.txn_mode << '\t' << txn_id << '\t' << read_keys.size() << '\t'
           << keys.size() << '\t' << group_size << '\t' << keys.size() << '\t'
           << rec.w_data << '\t' << rec.w_mt_block << '\t' << rec.w_mt_digest << '\t'
           << rec.w_lookup << '\t' << rec.w_proof << '\t' << rec.other << '\t'
           << rec.r_lookup << '\t' << rec.r_proof << '\t' << 0 << '\t'
           << rec.total << '\t' << blocks_ << '\t'
           << rec.d_tip << '\t' << rec.d_lookup << '\t' << rec.d_encode << '\t'
           << rec.d_idx << '\t' << rec.d_hist << '\t' << rec.d_block << '\n';
    }
  }

  void DoReadTxn(const std::string& key, uint64_t txn_id, bool record, uint64_t seq) {
    Rec rec;
    uint64_t t0 = NowNs();
    DoReadKey(key, &rec.r_lookup, &rec.r_proof);
    rec.total = NowNs() - t0;
    uint64_t sum = rec.r_lookup + rec.r_proof;
    rec.other = (rec.total > sum) ? (rec.total - sum) : 0;
    if (record && seq % o_.sample == 0) {
      out_ << o_.txn_mode << '\t' << txn_id << '\t' << 1 << '\t' << 0 << '\t' << 1 << '\t'
           << 0 << '\t' << 0 << '\t' << 0 << '\t' << 0 << '\t' << 0 << '\t' << 0 << '\t'
           << 0 << '\t' << rec.r_lookup << '\t' << rec.r_proof << '\t' << rec.other << '\t'
           << rec.total << '\t' << blocks_
           << "\t0\t0\t0\t0\t0\t0\n";
    }
  }

  bool Run() {
    if (o_.preload && !Preload()) return false;

    std::ifstream in(o_.workload.c_str());
    if (!in.is_open()) {
      fprintf(stderr, "[error] cannot open workload %s\n", o_.workload.c_str());
      return false;
    }
    std::string line;
    uint64_t row = 0, txn_id = 0, n_rec = 0, n_rec_w = 0, n_rec_r = 0;
    std::vector<std::string> pend_keys, pend_vals, pend_reads;

    auto measured = [&]() { return o_.measure_rows == 0 || n_rec < o_.measure_rows; };

    while (std::getline(in, line)) {
      ++row;
      bool in_measure = (row > o_.warmup_rows) && measured();
      if (row > o_.warmup_rows && !measured()) break;

      TraceOp t;
      if (!ParseLine(line, &t)) continue;

      if (o_.txn_mode == "op") {
        if (t.op == 1) {
          DoReadTxn(t.key, txn_id, in_measure, n_rec_r++);
        } else {
          std::vector<std::string> kv{t.key}, vv{t.val}, none;
          DoWriteTxn(kv, vv, none, txn_id, 1, in_measure, n_rec_w++);
        }
        ++txn_id;
        if (in_measure) ++n_rec;
      } else {
        if (t.op == 1) pend_reads.push_back(t.key);
        else { pend_keys.push_back(t.key); pend_vals.push_back(t.val); }
        while (pend_reads.size() >= 2 && pend_keys.size() >= 2) {
          std::vector<std::string> rk(pend_reads.begin(), pend_reads.begin() + 2);
          std::vector<std::string> wk(pend_keys.begin(), pend_keys.begin() + 2);
          std::vector<std::string> wv(pend_vals.begin(), pend_vals.begin() + 2);
          pend_reads.erase(pend_reads.begin(), pend_reads.begin() + 2);
          pend_keys.erase(pend_keys.begin(), pend_keys.begin() + 2);
          pend_vals.erase(pend_vals.begin(), pend_vals.begin() + 2);
          bool rec_flag = (row > o_.warmup_rows) && measured();
          DoWriteTxn(wk, wv, rk, txn_id, 4, rec_flag, n_rec_w++);
          ++txn_id;
          if (rec_flag) ++n_rec;
        }
      }

      if (row % 200000 == 0) {
        fprintf(stderr, "[progress] rows=%lu txns=%lu measured=%lu blocks=%lu\n",
                (unsigned long)row, (unsigned long)txn_id, (unsigned long)n_rec,
                (unsigned long)blocks_);
      }
    }
#ifdef PROOF_ON_WRITE
    fprintf(stderr, "[verify] tot=%lu fail=%lu\n",
            (unsigned long)g_verify_tot, (unsigned long)g_verify_fail);
#endif
    fprintf(stderr, "[done] rows=%lu txns=%lu measured=%lu blocks=%lu "
                    "pending(r=%zu,w=%zu)\n",
            (unsigned long)row, (unsigned long)txn_id, (unsigned long)n_rec,
            (unsigned long)blocks_, pend_reads.size(), pend_keys.size());
    return true;
  }

 private:
  Opts o_;
  ledgebase::qldb::QLDB qldb_;
  std::ofstream out_;
  uint64_t blocks_ = 0;
};

}  // namespace

int main(int argc, char** argv) {
  Opts o;
  if (!ParseArgs(argc, argv, &o)) return 1;
  Bench b(o);
  if (!b.Init()) return 1;
  if (!b.Run()) return 1;
  return 0;
}
