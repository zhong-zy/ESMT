#ifndef QLDB_QL_BTREE_PAGE_H_
#define QLDB_QL_BTREE_PAGE_H_

// 页式（page-oriented）B+ 树：4 KB 定长页 + 缓冲池 + 每批一条 WAL
//
// 与仓库原有 QLBTree（COW、每节点一次 rocksdb Put、整节点重新序列化）的区别：
//   - 节点是 4 KB 定长页，页内 slotted 布局，插入是**页内就地修改**；
//   - 一次 Set 只碰 O(depth) 个页；落盘按**脏页写回**，而不是"每个节点一次 Put"；
//   - 每批写追加一条 WAL 记录（模拟 redo log append）。
//
// 页仍然存放在同一个 rocksdb（key = prefix + "P" + pid），这样 A/B 对照只改变
// "索引更新策略"这一个变量（存储后端不变）。
//
// 三种写回模式（编译期 QLB_FLUSH）：
//   0 = 不写 WAL、不写回（纯内存，乐观下界）
//   1 = 每批一条 WAL，脏页留在池里、淘汰时写回（默认，最接近真实系统）
//   2 = 每批一条 WAL + 本批脏页立即写回（悲观上界）
//
// 不支持删除/合并（QLDB::Delete 本身是空实现）。

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "ledger/common/chunk.h"
#include "ledger/common/db.h"
#include "ledger/common/slice.h"

#ifndef QLB_FLUSH
#define QLB_FLUSH 1
#endif

namespace ledgebase {

namespace qldb {

class QLBTreePage {
 public:
  static constexpr uint32_t kPageSize = 4096;
  static constexpr uint32_t kNoNext = 0xFFFFFFFFu;

  QLBTreePage(DB* db, const std::string& prefix) noexcept;
  ~QLBTreePage() = default;

  Slice Get(const Slice& key) const;

  std::map<std::string, std::string> Range(const Slice& start,
                                           const Slice& end) const;

  bool Set(const Slice& key, const Slice& val);

  bool Set(const std::vector<Slice>& keys, const std::vector<Slice>& vals);

  inline uint64_t numElements() const { return num_elems_; }

 private:
  struct Frame {
    std::unique_ptr<unsigned char[]> buf;
    bool dirty = false;
  };

  // ---- 页访问 ----
  unsigned char* Fetch(uint32_t pid) const;      // 命中/载入（可能淘汰）
  uint32_t NewPage(bool leaf);                   // 分配新页（脏）
  void MarkDirty(uint32_t pid) const;
  void Store(uint32_t pid) const;                // 写回一页
  void EvictIfNeeded() const;

  // ---- 插入 ----
  // 返回 true 表示分裂：*sep = 右页最小键（内部页为空键），*new_pid = 右页
  bool InsertRec(uint32_t pid, const std::string& k, const std::string& v,
                 std::string* sep, uint32_t* new_pid);

  uint32_t Descend(const Slice& key) const;

  std::string PageKey(uint32_t pid) const;

  DB* db_;
  std::string prefix_;
  mutable std::unordered_map<uint32_t, Frame> pool_;
  mutable std::deque<uint32_t> fifo_;
  mutable std::vector<uint32_t> touched_;   // 本批次置脏的页（FLUSH=2 只写回这些）
  uint32_t root_ = 0;
  uint32_t next_pid_ = 1;
  uint64_t num_elems_ = 0;
  uint64_t wal_seq_ = 0;
  size_t pool_cap_ = 65536;   // 页数（默认 256 MB）
};

}  // namespace qldb

}  // namespace ledgebase

#endif  // QLDB_QL_BTREE_PAGE_H_
