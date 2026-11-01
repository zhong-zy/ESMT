#ifndef SHRUBS_LIKE_INDEX_INDEX_H
#define SHRUBS_LIKE_INDEX_INDEX_H

#include <iostream>
#include <cstdlib>

namespace indices {

    enum IndexType {
        MerkleTree,
        MPT,
        MBT,
        BucketTree,
        LETUS,
        EMPTY,
    };

    enum ShrubsType {
        NONE,
        SHRUBS,
    };

    class Index {
    public:
        virtual ~Index() = default;

        virtual void build(std::vector<KeyType>::iterator st, std::vector<KeyType>::iterator ed)=0;

        virtual void insert(KeyType &key, const std::string &value, bool update_hash)=0;

        virtual std::shared_ptr<std::string> search(KeyType &key)=0;

        virtual void Prove(KeyType &key, int64_t ts)=0;

        virtual void PointUpdateHash(FreqOrderItem op, bool immediate)=0;

        virtual void InlineUpdateHash(FreqOrderItem op) {}

        virtual void BatchUpdateHash(std::set<KeyType> *dirty_keys)=0;

        virtual void join()=0;

    public:
        virtual void ClearHash()=0;
        virtual uint64_t hash_update_cnt()=0;
    virtual uint64_t get_blocked_read()=0;

    virtual uint64_t get_prove_us_sum() { return 0; }
    virtual uint64_t get_prove_us_cnt() { return 0; }
        virtual uint64_t get_read_wait()=0;
        virtual uint64_t get_process_maintenance_wait_time()=0;
    virtual uint64_t get_hit_cnt()=0;
    virtual uint64_t get_loss_cnt()=0;

    virtual uint64_t get_pop_cnt() { return 0; }
    virtual uint64_t get_skip_stale_cnt() { return 0; }
    virtual uint64_t get_skip_clean_cnt() { return 0; }

    virtual uint64_t get_ads_queue_max() { return 0; }
    virtual uint64_t get_ads_queue_avg() { return 0; }
    virtual uint64_t get_dedup_merge_cnt() { return 0; }
    virtual uint64_t get_dedup_update_cnt() { return 0; }
    virtual uint64_t get_dedup_promote_cnt() { return 0; }

    virtual void reset_ads_stats() {}

    virtual void reset_stats() {}
        virtual uint64_t get_clean_us_sum() { return 0; }
        virtual uint64_t get_clean_us_cnt() { return 0; }
        virtual uint64_t get_key_gap_sum() { return 0; }
        virtual uint64_t get_clean_us_hist(int b) { return 0; }
        virtual uint64_t dirty_size()=0;
        virtual uint64_t ads_queue_size()=0;
    };

}

#endif
