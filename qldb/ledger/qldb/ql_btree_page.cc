#include "ledger/qldb/ql_btree_page.h"

#include <cstdlib>
#include <cstring>
#include <utility>

namespace ledgebase {

namespace qldb {

namespace {

// ---------- 页内布局 ----------
// [0]  u16 nslots
// [2]  u16 free_lo          （记录区从页尾向下生长）
// [4]  u32 next             （叶子链）
// [8]  u8  is_leaf
// [16] u16 slot[nslots]     （按 key 升序，指向记录偏移）
// 记录：u16 klen | u32 x | key[klen] | (叶子：val[x] / 内部：无 payload，x=child pid)
constexpr uint32_t HDR = 16;
constexpr uint32_t PAGE = QLBTreePage::kPageSize;

inline uint16_t Rd16(const unsigned char* p) { uint16_t v; std::memcpy(&v, p, 2); return v; }
inline uint32_t Rd32(const unsigned char* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
inline void Wr16(unsigned char* p, uint16_t v) { std::memcpy(p, &v, 2); }
inline void Wr32(unsigned char* p, uint32_t v) { std::memcpy(p, &v, 4); }

inline uint16_t NSlots(const unsigned char* p) { return Rd16(p); }
inline uint16_t FreeLo(const unsigned char* p) { return Rd16(p + 2); }
inline void SetFreeLo(unsigned char* p, uint16_t v) { Wr16(p + 2, v); }
inline uint32_t NextPid(const unsigned char* p) { return Rd32(p + 4); }
inline void SetNextPid(unsigned char* p, uint32_t v) { Wr32(p + 4, v); }
inline bool IsLeaf(const unsigned char* p) { return p[8] != 0; }

inline uint16_t SlotOff(const unsigned char* p, size_t i) { return Rd16(p + HDR + 2 * i); }
inline void SetSlotOff(unsigned char* p, size_t i, uint16_t v) { Wr16(p + HDR + 2 * i, v); }

inline uint16_t KeyLen(const unsigned char* p, uint16_t off) { return Rd16(p + off); }
inline const unsigned char* KeyPtr(const unsigned char* p, uint16_t off) { return p + off + 6; }
inline uint32_t RecX(const unsigned char* p, uint16_t off) { return Rd32(p + off + 2); }
inline void SetRecX(unsigned char* p, uint16_t off, uint32_t v) { Wr32(p + off + 2, v); }

inline size_t RecSize(const unsigned char* p, uint16_t off) {
  size_t klen = KeyLen(p, off);
  return 6 + klen + (IsLeaf(p) ? RecX(p, off) : 0);
}

// key(第 i 槽) 与 target 比较（-1/0/1）
inline int CmpSlot(const unsigned char* p, size_t i, const std::string& target) {
  uint16_t off = SlotOff(p, i);
  uint16_t klen = KeyLen(p, off);
  const unsigned char* kp = KeyPtr(p, off);
  size_t n = klen < target.size() ? klen : target.size();
  int c = std::memcmp(kp, target.data(), n);
  if (c != 0) return c < 0 ? -1 : 1;
  if (klen == target.size()) return 0;
  return klen < target.size() ? -1 : 1;
}

enum PutRes { kNoSpace = 0, kInserted = 1, kReplaced = 2 };

void ResetPage(unsigned char* p, bool leaf) {
  std::memset(p, 0, PAGE);
  Wr16(p, 0);                    // nslots
  SetFreeLo(p, static_cast<uint16_t>(PAGE));
  SetNextPid(p, QLBTreePage::kNoNext);
  p[8] = leaf ? 1 : 0;
}

// 追加一条记录（不做空间检查，调用方保证）
void AppendRaw(unsigned char* p, const std::string& k, const std::string& v,
               uint32_t child) {
  const bool leaf = IsLeaf(p);
  const uint32_t x = leaf ? static_cast<uint32_t>(v.size()) : child;
  const size_t rec = 6 + k.size() + (leaf ? v.size() : 0);
  const uint16_t off = static_cast<uint16_t>(FreeLo(p) - rec);
  Wr16(p + off, static_cast<uint16_t>(k.size()));
  Wr32(p + off + 2, x);
  if (!k.empty()) std::memcpy(p + off + 6, k.data(), k.size());
  if (leaf && !v.empty()) std::memcpy(p + off + 6 + k.size(), v.data(), v.size());
  const uint16_t n = NSlots(p);
  const uint16_t pos = static_cast<uint16_t>(n);
  SetSlotOff(p, pos, off);
  Wr16(p, static_cast<uint16_t>(n + 1));
  SetFreeLo(p, off);
}

// 取出所有存活记录（key, payload），用于压缩/分裂
void CollectLive(const unsigned char* p,
                 std::vector<std::pair<std::string, std::string>>* out) {
  const uint16_t n = NSlots(p);
  for (uint16_t i = 0; i < n; ++i) {
    const uint16_t off = SlotOff(p, i);
    const uint16_t klen = KeyLen(p, off);
    if (klen == 0 && off == 0) continue;             // 已删除槽
    std::string k(reinterpret_cast<const char*>(KeyPtr(p, off)), klen);
    std::string v;
    if (IsLeaf(p)) {
      const uint32_t vlen = RecX(p, off);
      v.assign(reinterpret_cast<const char*>(p + off + 6 + klen), vlen);
    } else {
      uint32_t child = RecX(p, off);
      v.assign(reinterpret_cast<const char*>(&child), 4);
    }
    out->emplace_back(std::move(k), std::move(v));
  }
}

bool Fits(const unsigned char* p, size_t rec_size) {
  return FreeLo(p) >= HDR + 2 * (static_cast<size_t>(NSlots(p)) + 1) + rec_size;
}

// 压缩：把存活记录重排到页尾，消除空洞
void Compact(unsigned char* p) {
  std::vector<std::pair<std::string, std::string>> live;
  live.reserve(NSlots(p));
  CollectLive(p, &live);
  const bool leaf = IsLeaf(p);
  const uint32_t next = NextPid(p);
  ResetPage(p, leaf);
  SetNextPid(p, next);
  for (auto& kv : live) {
    uint32_t child = 0;
    if (!leaf) std::memcpy(&child, kv.second.data(), 4);
    AppendRaw(p, kv.first, leaf ? kv.second : std::string(), child);
  }
}

// 分裂：左页就地重写，右页写到新页；*sep 返回分隔键（内部页为右页首条真实键）
void SplitPage(unsigned char* left, unsigned char* right, bool leaf,
               std::string* sep) {
  std::vector<std::pair<std::string, std::string>> live;
  CollectLive(left, &live);
  const size_t n = live.size();
  const size_t mid = n / 2;
  const uint32_t next = NextPid(left);
  ResetPage(left, leaf);
  ResetPage(right, leaf);
  for (size_t i = 0; i < mid; ++i) {
    uint32_t child = 0;
    if (!leaf) std::memcpy(&child, live[i].second.data(), 4);
    AppendRaw(left, live[i].first, leaf ? live[i].second : std::string(), child);
  }
  static const std::string kEmptyKey;
  for (size_t i = mid; i < n; ++i) {
    uint32_t child = 0;
    if (!leaf) std::memcpy(&child, live[i].second.data(), 4);
    // 只有内部页需要把首条记录改成 -inf；叶子页必须保留真实 key
    const std::string& kk = (!leaf && i == mid) ? kEmptyKey : live[i].first;
    AppendRaw(right, kk, leaf ? live[i].second : std::string(), child);
  }
  if (!leaf) {
    *sep = live[mid].first;                       // 右页首条真实键
  } else {
    *sep = live[mid].first;
    SetNextPid(right, next);
    SetNextPid(left, 0);                          // 由调用方填写右页 pid
  }
}

}  // namespace

QLBTreePage::QLBTreePage(DB* db, const std::string& prefix) noexcept
    : db_(db), prefix_(prefix) {
  const char* env = std::getenv("QLB_POOL_MB");
  if (env != nullptr) {
    long mb = std::atol(env);
    if (mb > 0) pool_cap_ = static_cast<size_t>(mb) * 1024 * 1024 / kPageSize;
  }
  std::string r;
  db_->Get(prefix_ + "ROOT", &r);
  if (r.size() == 4) {
    std::memcpy(&root_, r.data(), 4);
    if (root_ == 0) root_ = NewPage(true);
  } else {
    root_ = NewPage(true);
    std::string v(4, '\0');
    std::memcpy(&v[0], &root_, 4);
    db_->Put(prefix_ + "ROOT", v);
  }
}

std::string QLBTreePage::PageKey(uint32_t pid) const {
  return prefix_ + "P" + std::to_string(pid);
}

void QLBTreePage::Store(uint32_t pid) const {
  auto it = pool_.find(pid);
  if (it == pool_.end()) return;
  db_->Put(PageKey(pid), std::string(reinterpret_cast<const char*>(it->second.buf.get()), kPageSize));
}

void QLBTreePage::EvictIfNeeded() const {
  while (pool_.size() >= pool_cap_ && !fifo_.empty()) {
    uint32_t pid = fifo_.front();
    fifo_.pop_front();
    auto it = pool_.find(pid);
    if (it == pool_.end()) continue;
    if (it->second.dirty) Store(pid);
    pool_.erase(it);
  }
}

unsigned char* QLBTreePage::Fetch(uint32_t pid) const {
  auto it = pool_.find(pid);
  if (it != pool_.end()) return it->second.buf.get();
  // 注意：这里不做淘汰。淘汰只在操作入口做一次，避免正在使用的页被逐出。
  std::unique_ptr<unsigned char[]> buf(new unsigned char[kPageSize]);
  std::memset(buf.get(), 0, kPageSize);
  std::string blob;
  db_->Get(PageKey(pid), &blob);
  if (blob.size() >= kPageSize) {
    std::memcpy(buf.get(), blob.data(), kPageSize);
  } else {
    ResetPage(buf.get(), true);                 // 新页默认叶子
  }
  Frame f;
  f.buf = std::move(buf);
  f.dirty = false;
  auto ins = pool_.emplace(pid, std::move(f));
  fifo_.push_back(pid);
  return ins.first->second.buf.get();
}

void QLBTreePage::MarkDirty(uint32_t pid) const {
  auto it = pool_.find(pid);
  if (it == pool_.end()) return;
  if (!it->second.dirty) {
    it->second.dirty = true;
    touched_.push_back(pid);            // 只记录"由脏变脏"的那一次
  }
}

uint32_t QLBTreePage::NewPage(bool leaf) {
  const uint32_t pid = next_pid_++;
  std::unique_ptr<unsigned char[]> buf(new unsigned char[kPageSize]);
  ResetPage(buf.get(), leaf);
  Frame f;
  f.buf = std::move(buf);
  f.dirty = true;
  pool_.emplace(pid, std::move(f));
  fifo_.push_back(pid);
  return pid;
}

uint32_t QLBTreePage::Descend(const Slice& key) const {
  std::string target = key.ToString();
  uint32_t pid = root_;
  while (true) {
    const unsigned char* p = Fetch(pid);
    if (IsLeaf(p)) return pid;
    const size_t n = NSlots(p);
    size_t lo = 1, hi = n;                        // 第 0 条是 -inf
    while (lo < hi) {
      size_t mid = (lo + hi) / 2;
      if (CmpSlot(p, mid, target) <= 0) lo = mid + 1;
      else hi = mid;
    }
    pid = RecX(p, SlotOff(p, lo - 1));
  }
}

Slice QLBTreePage::Get(const Slice& key) const {
  EvictIfNeeded();
  const uint32_t pid = Descend(key);
  const unsigned char* p = Fetch(pid);
  std::string target = key.ToString();
  const size_t n = NSlots(p);
  size_t lo = 0, hi = n;
  while (lo < hi) {
    size_t mid = (lo + hi) / 2;
    if (CmpSlot(p, mid, target) < 0) lo = mid + 1;
    else hi = mid;
  }
  if (lo < n && CmpSlot(p, lo, target) == 0) {
    uint16_t off = SlotOff(p, lo);
    uint32_t vlen = RecX(p, off);
    static thread_local std::string scratch;
    scratch.assign(reinterpret_cast<const char*>(p + off + 6 + KeyLen(p, off)), vlen);
    return Slice(scratch.data(), scratch.size());
  }
  return Slice();
}

// 页内插入（不处理分裂）
PutRes PagePut(unsigned char* p, const std::string& k, const std::string& v,
               uint32_t child) {
  const bool leaf = IsLeaf(p);
  size_t n = NSlots(p);
  size_t pos = 0;
  {
    size_t lo = 0, hi = n;
    while (lo < hi) {
      size_t mid = (lo + hi) / 2;
      if (CmpSlot(p, mid, k) < 0) lo = mid + 1;
      else hi = mid;
    }
    pos = lo;
  }
  if (pos < n && CmpSlot(p, pos, k) == 0) {
    uint16_t off = SlotOff(p, pos);
    if (!leaf) {
      SetRecX(p, off, child);
      return kReplaced;
    }
    if (RecX(p, off) == v.size() && KeyLen(p, off) == k.size()) {
      std::memcpy(p + off + 6 + k.size(), v.data(), v.size());
      return kReplaced;
    }
    // 值长度变了：删槽 + 压缩后重插
    for (size_t i = pos + 1; i < n; ++i) SetSlotOff(p, i - 1, SlotOff(p, i));
    Wr16(p, static_cast<uint16_t>(n - 1));
    Compact(p);
    n = NSlots(p);
    pos = 0;
    size_t lo = 0, hi = n;
    while (lo < hi) {
      size_t mid = (lo + hi) / 2;
      if (CmpSlot(p, mid, k) < 0) lo = mid + 1;
      else hi = mid;
    }
    pos = lo;
  }
  const size_t rec = 6 + k.size() + (leaf ? v.size() : 0);
  if (!Fits(p, rec)) {
    Compact(p);
    if (!Fits(p, rec)) return kNoSpace;
    // 重新定位（压缩不改 key 顺序）
    size_t lo = 0, hi = NSlots(p);
    while (lo < hi) {
      size_t mid = (lo + hi) / 2;
      if (CmpSlot(p, mid, k) < 0) lo = mid + 1;
      else hi = mid;
    }
    pos = lo;
    n = NSlots(p);
  }
  const uint16_t off = static_cast<uint16_t>(FreeLo(p) - rec);
  Wr16(p + off, static_cast<uint16_t>(k.size()));
  Wr32(p + off + 2, leaf ? static_cast<uint32_t>(v.size()) : child);
  if (!k.empty()) std::memcpy(p + off + 6, k.data(), k.size());
  if (leaf && !v.empty()) std::memcpy(p + off + 6 + k.size(), v.data(), v.size());
  for (size_t i = n; i > pos; --i) SetSlotOff(p, i, SlotOff(p, i - 1));
  SetSlotOff(p, pos, off);
  Wr16(p, static_cast<uint16_t>(n + 1));
  SetFreeLo(p, off);
  return kInserted;
}

// 把第 i 个槽的键改成 nk（隔键变小），保持顺序与子指针不变
void PageReplaceKey(unsigned char* p, size_t i, const std::string& nk) {
  const uint32_t child = RecX(p, SlotOff(p, i));
  const size_t n = NSlots(p);
  for (size_t j = i + 1; j < n; ++j) SetSlotOff(p, j - 1, SlotOff(p, j));
  Wr16(p, static_cast<uint16_t>(n - 1));
  Compact(p);
  PagePut(p, nk, std::string(), child);
}

bool QLBTreePage::InsertRec(uint32_t pid, const std::string& k,
                            const std::string& v, std::string* sep,
                            uint32_t* new_pid) {
  unsigned char* p = Fetch(pid);
  if (IsLeaf(p)) {
    PutRes r = PagePut(p, k, v, 0);
    if (r != kNoSpace) {
      MarkDirty(pid);
      if (r == kInserted) ++num_elems_;
      return false;
    }
    // 页满：先分裂，再把本条插入正确的一半（否则会丢记录）
    const uint32_t rp = NewPage(true);
    unsigned char* right = Fetch(rp);
    std::string s;
    SplitPage(p, right, true, &s);
    SetNextPid(p, rp);
    r = (k < s) ? PagePut(p, k, v, 0) : PagePut(right, k, v, 0);
    if (r == kInserted) ++num_elems_;
    MarkDirty(pid);
    MarkDirty(rp);
    *sep = s;
    *new_pid = rp;
    return true;
  }

  const size_t n = NSlots(p);
  size_t lo = 1, hi = n;
  while (lo < hi) {
    size_t mid = (lo + hi) / 2;
    if (CmpSlot(p, mid, k) <= 0) lo = mid + 1;
    else hi = mid;
  }
  const size_t idx = lo - 1;
  const uint32_t child = RecX(p, SlotOff(p, idx));
  std::string csep;
  uint32_t cnew = 0;
  const bool split = InsertRec(child, k, v, &csep, &cnew);

  p = Fetch(pid);                                  // 递归可能淘汰本页
  // 若插入的 key 小于该子树原最小键，更新父页里的分隔键（否则会路由到左兄弟）
  if (idx > 0 && CmpSlot(p, idx, k) > 0) {
    PageReplaceKey(p, idx, k);
    MarkDirty(pid);
  }
  if (!split) return false;

  PutRes r = PagePut(p, csep, std::string(), cnew);
  if (r == kNoSpace) {
    const uint32_t rp = NewPage(false);
    unsigned char* right = Fetch(rp);
    std::string s;
    SplitPage(p, right, false, &s);
    // 分裂后把新分隔项插到正确的一半
    if (csep < s) PagePut(p, csep, std::string(), cnew);
    else PagePut(right, csep, std::string(), cnew);
    MarkDirty(pid);
    MarkDirty(rp);
    *sep = s;
    *new_pid = rp;
    return true;
  }
  MarkDirty(pid);
  return false;
}

bool QLBTreePage::Set(const Slice& key, const Slice& val) {
  return Set(std::vector<Slice>{key}, std::vector<Slice>{val});
}

bool QLBTreePage::Set(const std::vector<Slice>& keys,
                      const std::vector<Slice>& vals) {
  if (keys.size() != vals.size()) return false;
  EvictIfNeeded();
  touched_.clear();
  for (size_t i = 0; i < keys.size(); ++i) {
    std::string k = keys[i].ToString();
    std::string v = vals[i].ToString();
    std::string sep;
    uint32_t np = 0;
    if (InsertRec(root_, k, v, &sep, &np)) {
      const uint32_t nr = NewPage(false);
      unsigned char* r = Fetch(nr);
      AppendRaw(r, std::string(), std::string(), root_);   // -inf → 左子
      AppendRaw(r, sep, std::string(), np);                // sep → 右子
      MarkDirty(nr);
      root_ = nr;
      std::string vv(4, '\0');
      std::memcpy(&vv[0], &root_, 4);
      db_->Put(prefix_ + "ROOT", vv);
    }
  }
#if QLB_FLUSH >= 1
  {
    std::string wal(8, 'w');
    db_->Put(prefix_ + "W" + std::to_string(wal_seq_++), wal);
  }
#endif
#if QLB_FLUSH == 2
  // 只写回本批次触及过的页（而不是整个缓冲池）
  for (uint32_t pid : touched_) {
    auto it = pool_.find(pid);
    if (it != pool_.end() && it->second.dirty) {
      Store(pid);
      it->second.dirty = false;
    }
  }
#endif
  return true;
}

std::map<std::string, std::string> QLBTreePage::Range(const Slice& start,
                                                      const Slice& end) const {
  EvictIfNeeded();
  std::map<std::string, std::string> result;
  std::string s = start.ToString();
  std::string e = end.ToString();
  uint32_t pid = Descend(start);
  while (true) {
    const unsigned char* p = Fetch(pid);
    const size_t n = NSlots(p);
    for (size_t i = 0; i < n; ++i) {
      uint16_t off = SlotOff(p, i);
      std::string k(reinterpret_cast<const char*>(KeyPtr(p, off)), KeyLen(p, off));
      if (k < s) continue;
      if (k >= e) return result;
      uint32_t vlen = RecX(p, off);
      result.emplace(std::move(k),
                     std::string(reinterpret_cast<const char*>(p + off + 6 + KeyLen(p, off)), vlen));
    }
    uint32_t nx = NextPid(p);
    if (nx == kNoNext) break;
    pid = nx;
  }
  return result;
}

}  // namespace qldb

}  // namespace ledgebase
