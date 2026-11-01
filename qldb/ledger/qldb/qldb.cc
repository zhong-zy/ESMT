#include "ledger/qldb/qldb.h"

#include <sstream>
#include <string>
#include <chrono>

namespace ledgebase {

namespace qldb {

#ifdef QDB_PROF
// 计时插桩：由 bench_qldb_eth 读取（单位 ns，相对 QLDB::Set 入口）
thread_local uint64_t qdb_prof_ns[5] = {0, 0, 0, 0, 0};
thread_local uint64_t qdb_prof_sub[6] = {0, 0, 0, 0, 0, 0};
thread_local uint64_t qdb_prof_enc_blk = 0;   // 块内路径编码耗时
thread_local uint64_t qdb_prof_enc_dg = 0;    // 块间路径编码耗时
static inline uint64_t QdbProfNow() {
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}
#endif

bool QLProofResult::Verify(const Hash digest) {
  auto doc = Document::Encode(data.key, data.val, addr, meta);
  auto doc_hash = doc.hash();
  //std::cout << "verify height " << proof.size() << std::endl;
  for (size_t i = 0; i < proof.size(); ++i) {
    if (proof[i].empty()) {
      std::unique_ptr<byte_t[]> node(
          new byte_t[Hash::kByteLength]);
      memcpy(node.get(), doc_hash.value(), Hash::kByteLength);
      doc_hash = Hash::ComputeFrom(node.get(),
          Hash::kByteLength);
    } else {
      std::unique_ptr<byte_t[]> node(
          new byte_t[Hash::kByteLength * 2]);
      Hash h = Hash::FromBase32(proof[i]);
      if (pos[i] == 0) {
        memcpy(node.get(), h.value(), Hash::kByteLength);
        memcpy(node.get() + Hash::kByteLength, doc_hash.value(),
            Hash::kByteLength);
      } else {
        memcpy(node.get(), doc_hash.value(), Hash::kByteLength);
        memcpy(node.get() + Hash::kByteLength, h.value(),
            Hash::kByteLength);
      }
      doc_hash = Hash::ComputeFrom(node.get(),
          Hash::kByteLength * 2);
    }
  }
  return doc_hash == digest;
}

QLDB::QLDB(std::string dbpath) {
  db_.Open(dbpath);
  indexed_.reset(new QLB_INDEX_TREE(&db_, "COMMITTED_"));
  history_.reset(new QLB_INDEX_TREE(&db_, "HISTORY_"));
}

void QLDB::CreateLedger(const std::string& name) {
  db_.Put(name, "0");
}

Slice QLDB::GetData(const std::string& name,
    const std::string& key) const {
  auto result = GetCommitted(name, key);
  Document doc(&result);
  return doc.getData().val;
}

Chunk QLDB::GetCommitted(const std::string& name,
    const std::string& key) const {
  // auto result = db_.Get(key);
  // return Chunk(result->head());
  auto combined_key = key;
  auto result = indexed_->Get(Slice(combined_key));
  return Chunk(result.data());
}

std::map<std::string, std::string> QLDB::Range(const std::string& name,
    const std::string& from, const std::string& to) const {
  // auto result = db_.Get(name + "|" + key);
  // return Chunk(result->head());
  auto result = indexed_->Range(Slice(from), Slice(to));
  return result;
}

Chunk QLDB::GetVersion(const std::string& name, const std::string& key, 
    const size_t version) const {
  auto combined_key = key + "|" + std::to_string(version);
  // auto result = db_.Get(combined_key);
  // return Chunk(result->head());
  auto result = history_->Get(Slice(combined_key));
  return Chunk(result.data());
}

std::vector<Chunk> QLDB::GetHistory(const std::string& name,
    const std::string& key, size_t n) const {
  std::vector<Chunk> retval;
  auto result = GetCommitted(name, key);
  Document doc(&result);
  auto version = doc.getMetaData().version;
  retval.emplace_back(std::move(result));
  for (size_t i = version; (i > 0) && (version - i < n); --i) {
    result = GetVersion(name, key, i-1);
    retval.emplace_back(std::move(result));
  }
  return retval;
}

bool QLDB::Set(const std::string& name,
               const std::vector<std::string>& keys,
               const std::vector<std::string>& vals,
               std::vector<QLProofResult>* proofs) {
  if (keys.size() != vals.size()) {
    return false;
  } else if (keys.size() == 0) {
    return true;
  }

#ifdef QDB_PROF
  qdb_prof_ns[0] = QdbProfNow();
#endif

  // get block sequence number and hash
  std::string tip;
  db_.Get(name, &tip);

#ifdef QDB_PROF
  uint64_t _prof_tip_end = QdbProfNow();
  qdb_prof_sub[2] = _prof_tip_end - qdb_prof_ns[0];
  uint64_t _prof_t0 = _prof_tip_end;
  uint64_t _prof_lookup = 0, _prof_encode = 0;
#endif
  uint64_t seqno;
  Hash prev_hash;
  if (tip.compare("") == 0) {
    seqno = 0;
    //byte_t empty_char[Hash::kByteLength] = {0};
    prev_hash = Hash::ComputeFrom("0");
  } else {
    auto token_idx = tip.find("|");
    auto old_seqno = uint64_t(std::stoull(tip.substr(0, token_idx)));
    seqno = old_seqno + 1;
    prev_hash = Hash::FromBase32(tip.substr(token_idx+1));
  }
  auto block_key = name + "|" + std::to_string(seqno);

  // current time
  uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();

  // create documents
#ifdef PROOF_ON_WRITE
  std::vector<size_t> versions;
  versions.reserve(keys.size());
#endif
  std::vector<Chunk> documents;
  std::vector<Hash> proof;
  std::vector<Slice> ks, vs, hist_keys;
  std::vector<std::string> hist_key_holder;
  for (size_t i = 0; i < keys.size(); ++i) {
    // get latest
#ifdef QDB_PROF
    _prof_t0 = QdbProfNow();
#endif
    auto latest_chunk = GetCommitted(name, keys[i]);
#ifdef QDB_PROF
    _prof_lookup += QdbProfNow() - _prof_t0;
    _prof_t0 = QdbProfNow();
#endif
    size_t version = 0;
    if (!latest_chunk.empty()) {
      Document latest_doc(&latest_chunk);
      version = latest_doc.getMetaData().version + 1;
    }
    
    auto document = Document::Encode(Slice(keys[i]) ,Slice(vals[i]), 
        {Slice(name), seqno}, {i, version, now});
#ifdef PROOF_ON_WRITE
    versions.push_back(version);
#endif
    auto doc_hash = document.hash();
    proof.emplace_back(doc_hash.Clone());
#ifdef QDB_PROF
    _prof_encode += QdbProfNow() - _prof_t0;
#endif

    // use rocksdb
    // std::string indexed_key = keys[i];
    // db_.Put(indexed_key, document);
    // std::string history_key = keys[i] + "|" + std::to_string(version);
    // db_.Put(history_key, document);

    documents.emplace_back(std::move(document));

    // use b+ tree
    ks.emplace_back(keys[i]);
    vs.emplace_back(documents[i].head(), documents[i].numBytes());
    hist_key_holder.emplace_back(keys[i] + "|" + std::to_string(version));
  }
  for (size_t i = 0; i < hist_key_holder.size(); ++i) {
    hist_keys.emplace_back(hist_key_holder[i]);
  }
#ifdef QDB_PROF
  uint64_t _prof_t_idx = QdbProfNow();
#endif
  indexed_->Set(ks, vs);
#ifdef QDB_PROF
  uint64_t _prof_t_hist = QdbProfNow();
  qdb_prof_sub[3] = _prof_t_hist - _prof_t_idx;
#endif
  history_->Set(hist_keys, vs);
#ifdef QDB_PROF
  qdb_prof_sub[4] = QdbProfNow() - _prof_t_hist;
  qdb_prof_sub[0] = _prof_lookup;
  qdb_prof_sub[1] = _prof_encode;
#endif

#ifdef QDB_PROF
  qdb_prof_ns[1] = QdbProfNow();
#endif

  // block hash
  proof.emplace_back(prev_hash.Clone());

  byte_t stmt[9];
  byte_t type = 0;
  memcpy(stmt, &type, 1);
  memcpy(stmt, &now, 8);
  auto stmt_hash = Hash::ComputeFrom(stmt, 9);
  proof.emplace_back(stmt_hash.Clone());

  std::unique_ptr<byte_t[]> addr(new byte_t[name.size() + 16]);
  memcpy(addr.get(), name.c_str(), name.size());
  memcpy(addr.get(), &seqno, 8);
  memcpy(addr.get(), &now, 8);
  auto addr_hash = Hash::ComputeFrom(addr.get(), name.size() + 16);
  proof.emplace_back(addr_hash.Clone());

#ifdef PROOF_ON_WRITE
  std::vector<std::vector<std::string>> bpaths;
  std::vector<std::vector<size_t>> bposs;
  std::vector<std::string> dpath;
  std::vector<size_t> dpos;
  auto block_hash = calculateBlockHash(proof, name, seqno, &bpaths, &bposs);
#else
  auto block_hash = calculateBlockHash(proof, name, seqno);
#endif

#ifdef QDB_PROF
  qdb_prof_ns[2] = QdbProfNow();
#endif
#ifdef PROOF_ON_WRITE
  calculateDigest(block_hash, prev_hash, name, seqno, &dpath, &dpos);
#else
  calculateDigest(block_hash, prev_hash, name, seqno);
#endif

#ifdef QDB_PROF
  qdb_prof_ns[3] = QdbProfNow();
#endif

  // create block
  auto block = QLBlock::Encode({Slice(name), seqno}, {0, now},
      now, block_hash, prev_hash, documents);

  // write to ledger storage
  db_.Put(block_key, block);
  std::string new_tip = std::to_string(seqno) + "|" + block_hash.ToBase32();
  db_.Put(name, new_tip);

#ifdef PROOF_ON_WRITE
  if (proofs != nullptr) {
    proofs->clear();
    proofs->reserve(keys.size());
    for (size_t i = 0; i < keys.size(); ++i) {
      QLProofResult r;
      r.data.key = Slice(keys[i]);
      r.data.val = Slice(vals[i]);
      r.addr.ledger_name = Slice(name);
      r.addr.seq_no = seqno;
      r.meta.doc_seq = i;
      r.meta.version = (i < versions.size()) ? versions[i] : 0;
      r.meta.time = now;
      if (i < bpaths.size()) {
        r.proof = bpaths[i];
        r.pos = bposs[i];
      }
      r.proof.insert(r.proof.end(), dpath.begin(), dpath.end());
      r.pos.insert(r.pos.end(), dpos.begin(), dpos.end());
      proofs->push_back(std::move(r));
    }
  }
#endif

#ifdef QDB_PROF
  qdb_prof_ns[4] = QdbProfNow();
#endif

  return true;
}

bool QLDB::Delete(const std::string& name,
                  const std::vector<std::string>& keys) const {
  return true;
}

Hash QLDB::calculateBlockHash(const std::vector<Hash>& proof,
    const std::string& name, const uint64_t seqno,
    std::vector<std::vector<std::string>>* paths,
    std::vector<std::vector<size_t>>* poss) {
  size_t level = 0;
  size_t last = proof.size() - 1;
  std::vector<Hash> current;
  copy(proof.begin(), proof.end(), back_inserter(current));
  std::vector<size_t> flat;              // 每个叶子在当前层的下标
  if (paths != nullptr) {
    paths->assign(proof.size(), {});
    if (poss != nullptr) poss->assign(proof.size(), {});
    flat.resize(proof.size());
    for (size_t j = 0; j < flat.size(); ++j) flat[j] = j;
  }
  while (last > 0) {
    ++level;
    auto pr_last = last / 2;
    auto pr_last_idx = last % 2;
    if (paths != nullptr) {
      const uint64_t _e0 = QdbProfNow();
      for (size_t j = 0; j < flat.size(); ++j) {
        const size_t p = flat[j];
        const size_t sib = p ^ 1;
        const size_t pos = 1 - (p % 2);
        if (sib <= last) (*paths)[j].push_back(current[sib].ToBase32());
        else (*paths)[j].push_back(std::string());     // 单节点提升
        if (poss != nullptr) (*poss)[j].push_back(pos);
      }
      qdb_prof_enc_blk += QdbProfNow() - _e0;
    }

    for (size_t i = 0; i <= pr_last; ++i) {
      size_t node_size;
      if (i == pr_last && pr_last_idx == 0) {
        node_size = Hash::kByteLength;
      } else {
        node_size = Hash::kByteLength * 2;
      }
      std::unique_ptr<byte_t[]> node(new byte_t[node_size]);
      memcpy(node.get(), current[i*2].value(), Hash::kByteLength);
      if (node_size == Hash::kByteLength * 2) {  
        memcpy(node.get() + Hash::kByteLength, current[i*2+1].value(),
            Hash::kByteLength);
      }
      std::string key = "proof_" + name + "|" + std::to_string(seqno) + "|" +
          std::to_string(level) + "|" + std::to_string(i);
      db_.Put(key, Slice(node.get(), node_size).ToString());
      Hash pr_hash = Hash::ComputeFrom(node.get(), node_size);
      current.emplace_back(pr_hash.Clone());
    }
    current.erase(current.begin(), current.begin() + last + 1);
    if (paths != nullptr) { for (size_t j = 0; j < flat.size(); ++j) flat[j] /= 2; }
    last = pr_last;
  }
  return current[0].Clone();
}

void QLDB::calculateDigest(const Hash& block_hash, const Hash& prev_block_hash,
    const std::string& name, const uint64_t seqno,
    std::vector<std::string>* dpath, std::vector<size_t>* dpos) {
  auto last_seqno = seqno;
  auto curr_hash = block_hash.Clone();

  size_t level = 0;
  while (last_seqno > 0) {
    ++level;
    auto pr_last = last_seqno / 2;
    auto pr_last_idx = last_seqno % 2;

    std::string key = "proof_" + name + "|" + std::to_string(level) + "|" +
        std::to_string(pr_last);
    if (pr_last_idx == 0) {
      byte_t node[Hash::kByteLength];
      memcpy(node, curr_hash.value(), Hash::kByteLength);
      db_.Put(key, Slice(node, Hash::kByteLength).ToString());
      if (dpath != nullptr) {
        const uint64_t _e0 = QdbProfNow();
        dpath->push_back(std::string());              // 单节点提升
        if (dpos != nullptr) dpos->push_back(1);
        qdb_prof_enc_dg += QdbProfNow() - _e0;
      }
      curr_hash = Hash::ComputeFrom(node, Hash::kByteLength);
    } else {
      Hash prev_hash;
      if (level == 1) {
        prev_hash = prev_block_hash;
      } else {
        std::string prev_key = "proof_" + name + "|" + std::to_string(level-1) +
            "|" + std::to_string(last_seqno - 1);
        std::string prev_node_str;
        db_.Get(prev_key, &prev_node_str);
        Slice prev_node(prev_node_str);
        prev_hash = Hash::ComputeFrom(prev_node.data(), Hash::kByteLength * 2);
      }
      if (dpath != nullptr) {
        const uint64_t _e0 = QdbProfNow();
        dpath->push_back(prev_hash.ToBase32());       // 左侧兄弟（已读，零额外 I/O）
        if (dpos != nullptr) dpos->push_back(0);
        qdb_prof_enc_dg += QdbProfNow() - _e0;
      }
      byte_t node[Hash::kByteLength * 2];
      memcpy(node, prev_hash.value(), Hash::kByteLength);
      memcpy(node + Hash::kByteLength, curr_hash.value(), Hash::kByteLength);
      db_.Put(key, Slice(node, Hash::kByteLength * 2).ToString());
      curr_hash = Hash::ComputeFrom(node, Hash::kByteLength * 2);
    }
    last_seqno = pr_last;
  }

  std::string key = "digest_" + name;
  std::string digest = curr_hash.ToBase32();
  db_.Put(key, digest);
}

DigestInfo QLDB::digest(const std::string& name) {
  std::string key = "digest_" + name;
  std::string digest, tip;
  db_.Get(key, &digest);
  db_.Get(name, &tip);
  if (tip.compare("") == 0) {
    return {0, digest};
  } else {
    auto token_idx = tip.find("|");
    auto seqno = uint64_t(std::stoull(tip.substr(0, token_idx)));
    return {seqno, digest};
  }
}

QLProofResult QLDB::getProof(const std::string& name, const uint64_t tip,
    const uint64_t block_addr, const uint32_t doc_seq) {
  if (block_addr > tip) return {};
  std::string block_key = name + "|" + std::to_string(block_addr);
  auto block = db_.Get(block_key);
  QLBlock qb(block);
  auto doc_size = qb.getDocumentSize() + 2;
  if (doc_seq > doc_size) return {};
  Chunk chunk = qb.getDocumentBySeq(doc_seq);
  Document doc(&chunk);
  QLProofResult result;
  result.addr = doc.getAddr();
  result.data = doc.getData();
  result.meta = doc.getMetaData();

  size_t level = 0;
  size_t curr_seq = doc_seq;
  while (doc_size > 0) {
    ++level;
    auto pr_seq = curr_seq / 2;
    auto pr_seq_idx = 1 - curr_seq % 2;
    result.pos.emplace_back(pr_seq_idx);
    if (curr_seq == doc_size && doc_size % 2 == 0) {
      result.proof.emplace_back(Hash().ToBase32());
    } else {
      std::string key = "proof_" + name + "|" + std::to_string(block_addr) +
          "|" + std::to_string(level) + "|" + std::to_string(pr_seq);
      std::string nodestr;
      db_.Get(key, &nodestr);
      Slice node(nodestr);
      Hash hash(node.data() + Hash::kByteLength * pr_seq_idx);
      // std::cerr << level << ", " << pr_seq << ": " << Hash(node.data()) << ", " << Hash(node.data() + Hash::kByteLength) << std::endl;
      result.proof.emplace_back(hash.ToBase32());
    }
    curr_seq = pr_seq;
    doc_size /= 2;
  }
  //std::cout << "block " << level << std::endl;

  level = 0;
  curr_seq = block_addr;
  auto latest = tip;
  while (latest > 0) {
    ++level;
    auto pr_seq = curr_seq / 2;
    auto pr_seq_idx = 1 - curr_seq % 2;
    result.pos.emplace_back(pr_seq_idx);
    std::string key = "proof_" + name + "|" + std::to_string(level) + "|" +
        std::to_string(pr_seq);
    if (curr_seq == latest && latest % 2 == 0) {
      result.proof.emplace_back(Hash().ToBase32());
    } else {
      std::string nodestr;
      db_.Get(key, &nodestr);
      Slice node(nodestr);
      // std::cerr << "block_" << level << ", " << pr_seq << ": " << Hash(node.data()) << ", " << Hash(node.data() + Hash::kByteLength) << std::endl;
      Hash hash(node.data() + pr_seq_idx * Hash::kByteLength);
      result.proof.emplace_back(hash.ToBase32());
    }
    curr_seq = pr_seq;
    latest /= 2;
  }
  //std::cout << "height " << level << std::endl;
  return result;
}

}  // namespace qldb

}  // namespace ledgebase
