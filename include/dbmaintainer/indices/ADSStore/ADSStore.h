
#ifndef SHRUBS_LIKE_INDEX_ADSSTORE_H
#define SHRUBS_LIKE_INDEX_ADSSTORE_H

#include <string>
#include <memory>

namespace indices {
    enum ADSStoreType {
        ROCKSDB=0,
        DISK,
        Memory,
    };

    class ADSStore {
    public:
        virtual ~ADSStore() = default;
        virtual std::shared_ptr<std::string> Get(uint64_t tree_id, const std::string &key) = 0;

        virtual void Put(uint64_t tree_id, const std::string &key, const std::string &value) = 0;
    };
}

#endif
