
#ifndef SHRUBS_LIKE_INDEX_ADSSTOREROCKSDB_H
#define SHRUBS_LIKE_INDEX_ADSSTOREROCKSDB_H

#include "ADSStore.h"
#include "rocksdb/db.h"
#include <iostream>

using namespace rocksdb;
namespace indices {
    class ADSStoreRocksDB: public ADSStore {
        std::unique_ptr<DB> db;
        std::string db_path;
    public:
        ADSStoreRocksDB(std::string path="./rocksdb") {
            db_path = path;
            Options options;
            options.create_if_missing = true;
            DB* raw_db = nullptr;
            Status status = DB::Open(options, path, &raw_db);
            db.reset(raw_db);
            if (status.ok())
                std::cout << "db ok" << std::endl;
        }

        ~ADSStoreRocksDB() {
            db.reset();
            std::string cmd = "rm -rf " + db_path;
            system(cmd.c_str());
        }

        std::string getKey(uint64_t tree_id, const std::string &key) {
            return std::to_string(tree_id) + "|" + key;
        }

        std::shared_ptr<std::string> Get(uint64_t tree_id, const std::string &key) {
            auto value = std::make_shared<std::string>();
            db->Get(ReadOptions(), getKey(tree_id, key), value.get());
            return value;
        }

        void Put(uint64_t tree_id, const std::string &key, const std::string &value) {
            db->Put(WriteOptions(), getKey(tree_id, key), value);
        }
    };
}

#endif
