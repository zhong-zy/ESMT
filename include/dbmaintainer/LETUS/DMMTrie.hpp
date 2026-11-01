#ifndef _DMMTRIE_HPP_
#define _DMMTRIE_HPP_

#include <array>
#include <cstring>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "config.h"
#include "workloads/workloads.h"
#include "dbmaintainer/LETUS/VDLS.hpp"
#include "dbmaintainer/LETUS/common.hpp"

static constexpr size_t HASH_SIZE = 32;
static constexpr size_t DMM_NODE_FANOUT = 16;
static constexpr uint16_t Td_ = 128;
static constexpr uint16_t Tb_ = 256;

typedef uint64_t KeyType;
#include <algorithm>

using namespace std;

static std::string BuildKeyName(uint64_t key_num, int key_len) {
 std::string key_num_str = std::to_string(key_num);
 int zeros = key_len - key_num_str.length();
 zeros = std::max(0, zeros);
 std::string key_name = "";
 return key_name.append(zeros, '0').append(key_num_str);
}

namespace letus {
 class LSVPS;
class DMMTrie;
class DeltaPage;

string HashFunction(const string &input);

struct NodeProof {
  int level;
  int index;
  uint16_t bitmap;
  vector<string> sibling_hash;
};

struct DMMTrieProof {
  string value;
  vector<NodeProof> proofs;
};

class Node {
 public:
  virtual ~Node() = default;

  virtual void CalculateHash();
  virtual void SerializeTo(char *buffer, size_t &current_size,
                           bool is_root) const = 0;
  virtual void DeserializeFrom(char *buffer, size_t &current_size,
                               bool is_root) = 0;
  virtual void AddChild(int index, Node *child, uint64_t version,
                        const string &hash);
  virtual Node *GetChild(int index) const;
  virtual bool HasChild(int index) const;
  virtual void SetChild(int index, uint64_t version, string hash);
  virtual string GetChildHash(int index);
  virtual uint64_t GetChildVersion(int index);
  virtual void UpdateNode();
  virtual void SetLocation(tuple<uint64_t, uint64_t, uint64_t> location);

  virtual string GetHash() = 0;
  virtual uint64_t GetVersion() = 0;
  virtual void SetVersion(uint64_t version) = 0;
  virtual void SetHash(string hash) = 0;

  virtual bool IsLeaf() const = 0;

  virtual NodeProof GetNodeProof(int level, int index);
};

class LeafNode : public Node {
 public:
  LeafNode(uint64_t V = 0, const string &k = "",
           const tuple<uint64_t, uint64_t, uint64_t> &l = {},
           const string &h = "");
  void CalculateHash(const string &value);
  void SerializeTo(char *buffer, size_t &current_size,
                   bool is_root) const override;
  void DeserializeFrom(char *buffer, size_t &current_size,
                       bool is_root) override;
  void UpdateNode(uint64_t version,
                  const tuple<uint64_t, uint64_t, uint64_t> &location,
                  const string &value, uint8_t location_in_page,
                  DeltaPage *deltapage);
  tuple<uint64_t, uint64_t, uint64_t> GetLocation() const;
  void SetLocation(tuple<uint64_t, uint64_t, uint64_t> location) override;
  string GetHash();
  uint64_t GetVersion();
  void SetVersion(uint64_t version);
  void SetHash(string hash);
  bool IsLeaf() const override;

 private:
  uint64_t version_;
  string key_;
  tuple<uint64_t, uint64_t, uint64_t>
      location_;
  string hash_;
  const bool is_leaf_;
};

class IndexNode : public Node {
 public:
  IndexNode(uint64_t V = 0, const string &h = "", uint16_t b = 0);
  IndexNode(
      uint64_t version, const string &hash, uint16_t bitmap,
      const array<tuple<uint64_t, string, Node *>, DMM_NODE_FANOUT> &children);
  IndexNode(const IndexNode &other);
  void CalculateHash() override;
  void SerializeTo(char *buffer, size_t &current_size,
                   bool is_root) const override;
  void DeserializeFrom(char *buffer, size_t &current_size,
                       bool is_root) override;
  void UpdateNode(uint64_t version, int index, const string &child_hash,
                  uint8_t location_in_page, DeltaPage *deltapage);
  void AddChild(int index, Node *child, uint64_t version = 0,
                const string &hash = "") override;
  Node *GetChild(int index) const override;
  bool HasChild(int index) const override;
  void SetChild(int index, uint64_t version, string hash) override;
  string GetChildHash(int index);
  uint64_t GetChildVersion(int index);
  string GetHash();
  uint64_t GetVersion();
  void SetVersion(uint64_t version);
  void SetHash(string hash);
  bool IsLeaf() const override;
  NodeProof GetNodeProof(int level, int index);

 private:
  uint64_t version_;
  string hash_;
  uint16_t bitmap_;
  array<tuple<uint64_t, string, Node *>, DMM_NODE_FANOUT> children_;
  const bool is_leaf_;
};

class DeltaPage : public Page {
 public:
  struct DeltaItem {
    uint8_t location_in_page;
    bool is_leaf_node;
    uint64_t version;
    string hash;

    uint64_t fileID;
    uint64_t offset;
    uint64_t size;

    uint8_t index;
    string child_hash;

    DeltaItem() {}
    DeltaItem(uint8_t loc, bool leaf, uint64_t ver, const string &h,
              uint64_t fID = 0, uint64_t off = 0, uint64_t sz = 0,
              uint8_t idx = 0, const string &ch_hash = "");
    DeltaItem(char *buffer, size_t &current_size);
    void SerializeTo(std::ofstream &out) const;
    void SerializeTo(char *buffer, size_t &current_size) const;
    bool Deserialize(std::ifstream &in);
  };

  DeltaPage(PageKey last_pagekey = {0, 0, true, ""}, uint16_t update_count = 0,
            uint16_t b_update_count = 0);
  DeltaPage(char *buffer);
  DeltaPage(const DeltaPage &other);
  ~DeltaPage();
  void AddIndexNodeUpdate(uint8_t location, uint64_t version,
                          const string &hash, uint8_t index,
                          const string &child_hash);
  void AddLeafNodeUpdate(uint8_t location, uint64_t version, const string &hash,
                         uint64_t fileID, uint64_t offset, uint64_t size);
  void SerializeTo();
  void ClearDeltaPage();
  const vector<DeltaItem> &GetDeltaItems() const;
  PageKey GetLastPageKey() const;
  void SetLastPageKey(PageKey pagekey);
  uint16_t GetDeltaPageUpdateCount();
  uint16_t GetBasePageUpdateCount();
  void ClearBasePageUpdateCount();
  void SerializeTo(std::ofstream &out) const;
  bool Deserialize(std::ifstream &in);
  bool Deserialize(char *buffer);

 private:
  vector<DeltaItem> deltaitems_;
  PageKey last_pagekey_;
  uint16_t update_count_;
  uint16_t b_update_count_;
};

class BasePage : public Page {
 public:
  BasePage(DMMTrie *trie = nullptr, Node *root = nullptr,
           const string &pid = "");
  BasePage(DMMTrie *trie, char *buffer);
  BasePage(DMMTrie *trie, string key, string pid, string nibbles);
  BasePage(const BasePage &other);
  ~BasePage();
  void SerializeTo();
  void UpdatePage(uint64_t version,
                  tuple<uint64_t, uint64_t, uint64_t> location,
                  const string &value, const string &nibbles,
                  const string &child_hash, DeltaPage *deltapage,
                  PageKey pagekey);
  void UpdateDeltaItem(const DeltaPage::DeltaItem &deltaitem);
  Node *GetRoot() const;

 private:
  DMMTrie *trie_;
  Node *root_;
};

class DMMTrie {
 public:
  DMMTrie(uint64_t tid, LSVPS *page_store, VDLS *value_store,
          uint64_t current_version = 0);
  ~DMMTrie();
  bool Put(uint64_t tid, uint64_t version, const string &key,
           const string &value);
  string Get(uint64_t tid, uint64_t version, const string &key);
  void Delete(uint64_t tid, uint64_t version, const string &key);
  void Commit(uint64_t version);
  void CalcRootHash(uint64_t tid, uint64_t version);
  string GetRootHash(uint64_t tid, uint64_t version);
  DMMTrieProof GetProof(uint64_t tid, uint64_t version, const string &key);
  bool Verify(uint64_t tid, const string &key, const string &value,
              string root_hash, DMMTrieProof proof);
  bool Verify(uint64_t tid, uint64_t version, string root_hash);
  void Flush(uint64_t tid, uint64_t version);
  void Revert(uint64_t tid, uint64_t version);
  DeltaPage *GetDeltaPage(const string &pid);
  pair<uint64_t, uint64_t> GetPageVersion(PageKey pagekey);
  PageKey GetLatestBasePageKey(PageKey pagekey) const;
  void UpdatePageVersion(PageKey pagekey, uint64_t current_version,
                         uint64_t latest_basepage_version);
  void WritePageCache(PageKey pagekey, Page *page);
  void AddDeltaPageVersion(const string &pid, uint64_t version);
  uint64_t GetVersionUpperbound(const string &pid, uint64_t version);

 void build(vector<KeyType>::iterator st, vector<KeyType>::iterator ed) {
  std::sort(st, ed);

  auto key_len = to_string(workloads::init_record_count).length();
  for (auto it = st; it != ed; ++it) {

   auto key = BuildKeyName(*it, key_len);

   this->Put(0, 1, key, key);
  }
  this->Commit(1);
 }

 private:
  LSVPS *page_store_;
  VDLS *value_store_;
  uint64_t tid;
  BasePage *root_page_;
  uint64_t current_version_;
  unordered_map<PageKey, list<pair<PageKey, BasePage *>>::iterator,
                PageKey::Hash>
      lru_cache_;
  list<pair<PageKey, BasePage *>> pagekeys_;
  const size_t max_cache_size_ = 3000000;
  bool memory_only = true;
  unordered_map<string, DeltaPage>
      active_deltapages_;

  unordered_map<string, pair<uint64_t, uint64_t>>
      page_versions_;
  map<PageKey, Page *> page_cache_;
  map<string, string> put_cache_;
  unordered_map<string, vector<uint64_t>>
      deltapage_versions_;

  BasePage *GetPage(const PageKey &pagekey);
  void PutPage(const PageKey &pagekey, BasePage *page);
  void UpdatePageKey(const PageKey &old_pagekey, const PageKey &new_pagekey);
  string RecursiveVerify(PageKey pagekey);
};
}

#endif