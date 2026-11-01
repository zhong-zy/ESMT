
#ifndef SHRUBS_LIKE_INDEX_ADSSTOREMTMEMORY_H
#define SHRUBS_LIKE_INDEX_ADSSTOREMTMEMORY_H

#include "ADSStore.h"

namespace indices {
    class ADSStoreMTMemory: public ADSStore {
    public:
        std::shared_ptr<std::string> Get(uint64_t tree_id, const std::string &key) {
            return nullptr;
        }

        void Put(uint64_t tree_id, const std::string &key, const std::string &value) {}
    };
}

#endif
