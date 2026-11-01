
#ifndef SHRUBS_LIKE_INDEX_STREAM_H
#define SHRUBS_LIKE_INDEX_STREAM_H

#include <iostream>
#include <fstream>

namespace indices {
    class Location {
    public:
        uint64_t file_id;
        uint64_t offset;
        uint64_t size;
    };

    class StreamFileStore {
    public:
        Location write(const std::string &key, const std::string &value, uint64_t version) {
            std::string version_str = std::to_string(version);
            size_t record_size = version_str.size() + key.size() + value.size() + 3;
        }

    private:
        const uint64_t MaxFileSize = 64 * 1024 * 1024;
    };
}

#endif
