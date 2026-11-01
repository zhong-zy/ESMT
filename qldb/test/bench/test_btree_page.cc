// test_btree_page.cc —— QLBTreePage 功能自检
// 用法： ./test_btree_page [N] [db_dir]

#include <cstdio>
#include <cstdlib>
#include <map>
#include <random>
#include <string>

#include "ledger/qldb/ql_btree_page.h"

using ledgebase::Slice;
using ledgebase::qldb::QLBTreePage;

static std::string Key(int i) {
  char b[32];
  snprintf(b, sizeof(b), "key%08d", i);
  return std::string(b);
}
static std::string Val(int i, int g) {
  char b[64];
  snprintf(b, sizeof(b), "val%08d_g%d", i, g);
  return std::string(b);
}

int main(int argc, char** argv) {
  const int n = argc > 1 ? atoi(argv[1]) : 50000;
  const char* dir = argc > 2 ? argv[2] : "/tmp/btree_page_test";

  ledgebase::DB db;
  db.Open(dir);

  int bad = 0;
  {
    QLBTreePage t(&db, "T_");
    std::map<std::string, std::string> ref;
    for (int i = 0; i < n; ++i) {
      std::string k = Key(i), v = Val(i, 0);
      Slice ks(k), vs(v);
      t.Set(ks, vs);
      ref[k] = v;
    }
    for (int i = 0; i < n; ++i) {
      std::string k = Key(i);
      Slice ks(k);
      Slice g = t.Get(ks);
      if (g.len() == 0 || g.ToString() != ref[k]) ++bad;
    }
    printf("[seq]       bad=%d elem=%llu (expect %d)\n", bad,
           (unsigned long long)t.numElements(), n);

    // 全量 Range：看叶子链上到底有多少 key、是否有序、缺哪些
    {
      std::string lo = "a", hi = "zzzzzzzzzzzz";
      Slice slo(lo), shi(hi);
      auto all = t.Range(slo, shi);
      printf("[range-all] keys=%zu (expect %d)\n", all.size(), n);
      int shown = 0;
      for (auto& kv : all) { (void)kv; }
      for (int i = 0; i < n && shown < 5; ++i) {
        std::string k = Key(i);
        if (!all.count(k)) { printf("  missing: %s\n", k.c_str()); ++shown; }
      }
      // 检查有序性（map 已排序，这里只统计重复）
      printf("[range-all] distinct=%zu\n", all.size());
    }

    std::mt19937 rng(7);
    int m = n / 5;
    for (int j = 0; j < m; ++j) {
      int i = static_cast<int>(rng() % n);
      std::string k = Key(i), v = Val(i, 1);
      Slice ks(k), vs(v);
      t.Set(ks, vs);
      ref[k] = v;
    }
    int bad2 = 0;
    for (int i = 0; i < n; ++i) {
      std::string k = Key(i);
      Slice ks(k);
      Slice g = t.Get(ks);
      if (g.len() == 0 || g.ToString() != ref[k]) ++bad2;
    }
    printf("[overwrite] bad=%d elem=%llu\n", bad2, (unsigned long long)t.numElements());

    int miss = 1;
    for (int i = n; i < n + 500; ++i) {
      std::string k = Key(i);
      Slice ks(k);
      if (t.Get(ks).len() != 0) { miss = 0; break; }
    }

    std::string from = Key(n / 4), to = Key(n / 2);
    Slice fs(from), ts(to);
    auto r = t.Range(fs, ts);
    size_t expect = 0;
    for (auto it = ref.lower_bound(from); it != ref.end() && it->first < to; ++it) ++expect;
    printf("[miss=%d] [range] got=%zu expect=%zu\n", miss, r.size(), expect);
    bad += bad2;
    if (!miss || r.size() != expect || t.numElements() != (uint64_t)n) ++bad;
  }

  // 逆序插入（最左插）
  {
    QLBTreePage t2(&db, "T2_");
    for (int i = n - 1; i >= 0; --i) {
      std::string k = Key(i), v = Val(i, 9);
      Slice ks(k), vs(v);
      t2.Set(ks, vs);
    }
    int bad3 = 0;
    for (int i = 0; i < n; ++i) {
      std::string k = Key(i), v = Val(i, 9);
      Slice ks(k);
      Slice g = t2.Get(ks);
      if (g.len() == 0 || g.ToString() != v) ++bad3;
    }
    printf("[desc]      bad=%d elem=%llu\n", bad3, (unsigned long long)t2.numElements());
    bad += bad3;
  }

  printf("\nRESULT: %s\n", bad == 0 ? "PASS" : "FAIL");
  return bad == 0 ? 0 : 1;
}
