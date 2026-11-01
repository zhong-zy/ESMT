#ifndef _LSVPS_H_
#define _LSVPS_H_

#include <cstdint>
#include <queue>
#include <string>
#include <vector>

#include "dbmaintainer/LETUS/DMMTrie.hpp"
#include "dbmaintainer/LETUS/common.hpp"

namespace letus {

    struct IndexBlock {
        static constexpr size_t INDEXBLOCK_SIZE = 12288;

        struct Mapping {
            PageKey pagekey;
            uint64_t location;
        };

        static constexpr size_t MAPPINGS_PER_BLOCK = 126;

        IndexBlock();
        bool AddMapping(const PageKey &pagekey, uint64_t location);
        bool IsFull() const;
        const std::vector<Mapping> &GetMappings() const;
        bool SerializeTo(std::ofstream &out) const;
        bool Deserialize(std::ifstream &in);

    private:
        std::vector<Mapping> mappings_;
    };

    struct IndexFile {
        PageKey min_pagekey;
        PageKey max_pagekey;
        std::string filepath;
    };

    struct LookupBlock {
        static const size_t BLOCK_SIZE = 12288;

        std::vector<std::pair<PageKey, size_t>>
            entries;
        bool SerializeTo(std::ostream &out) const;
        bool Deserialize(std::istream &in);
    };

    class LSVPS {
    public:
        LSVPS(std::string index_file_path = ".",
              std::string delta_cache_dir = "./delta_cache")
            : cache_(),
              table_(*this),
              index_file_path_(index_file_path),
              active_delta_page_cache_(3000000, index_file_path) {}
        Page *PageQuery(uint64_t version);
        BasePage *LoadPage(const PageKey &pagekey);
        void StorePage(Page *page);
        void AddIndexFile(const IndexFile &index_file);
        int GetNumOfIndexFile();
        void RegisterTrie(DMMTrie *DMM_trie);
        const std::vector<Page *> &GetTable() const;
        void Flush();
        void StoreActiveDeltaPage(DeltaPage *page);
        DeltaPage *GetActiveDeltaPage(const string &pid);

    private:

        class blockCache {};

        class MemIndexTable {
        public:
            explicit MemIndexTable(LSVPS &parent);
            const std::vector<Page *> &GetBuffer() const;
            void Store(Page *page);
            bool IsFull() const;
            void Flush();

        private:
            void writeToStorage(const std::vector<IndexBlock> &index_blocks,
                                const LookupBlock &lookup_blocks,
                                const std::filesystem::path &filepath);
            std::vector<Page *> buffer_;

            const size_t max_size_ = 15876;
            LSVPS &parent_LSVPS_;
        };

        class ActiveDeltaPageCache {
        public:
            ActiveDeltaPageCache(size_t max_size, std::string cache_dir);
            ~ActiveDeltaPageCache();
            void Store(DeltaPage *page);
            DeltaPage *Get(const string &pid);

            void FlushToDisk();

        private:
            void evictIfNeeded();

            void prepareForBatchWrite(const string &pid, DeltaPage *page);

            void writePageToDisk(const string &pid, DeltaPage *page);

            bool readFromDisk(const string &pid, DeltaPage *page);
            void writeIndexBlock();
            void readIndexBlock();

            DeltaPage *page_pool_;
            std::queue<size_t> free_pages_;
            unordered_map<string, size_t> cache_;
            unordered_map<string, size_t> pid_to_offset_;
            const size_t max_size_;
            std::string cache_dir_;
            std::string cache_file_;
            std::list<string> lru_queue_;
        };

        Page *pageLookup(const PageKey &pagekey);
        Page *readPageFromIndexFile(std::vector<IndexFile>::const_iterator file_it,
                                    const PageKey &pagekey);
        void applyDelta(BasePage *basepage, const DeltaPage *deltapage,
                        PageKey pagekey);

        blockCache cache_;
        MemIndexTable table_;
        std::string index_file_path_;
        std::string delta_cache_dir_;
        ActiveDeltaPageCache active_delta_page_cache_;
        DMMTrie *trie_;
        std::vector<IndexFile> index_files_;
    };
}

#endif