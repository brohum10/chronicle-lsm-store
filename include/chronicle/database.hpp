#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <vector>

#include "chronicle/skip_list.hpp"
#include "chronicle/sstable.hpp"
#include "chronicle/wal.hpp"

namespace chronicle {

struct Options {
    std::size_t memtable_max_entries{1'024};
    std::size_t compaction_trigger{4};
    std::size_t max_key_bytes{4 * 1'024};
    std::size_t max_value_bytes{4 * 1'024 * 1'024};
    bool sync_writes{true};
};

struct Stats {
    std::size_t memtable_entries{};
    std::size_t sstable_count{};
    std::uint64_t sequence{};
    std::uint64_t writes{};
    std::uint64_t reads{};
    std::uint64_t bloom_filter_negatives{};
};

class Database {
public:
    explicit Database(std::filesystem::path directory, Options options = {});
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    void put(std::string key, std::string value);
    void erase(std::string key);
    [[nodiscard]] std::optional<std::string> get(std::string_view key) const;
    void flush();
    void compact();
    [[nodiscard]] Stats stats() const;

private:
    void validate(std::string_view key, std::string_view value) const;
    void apply_write(std::string key, std::string value, bool tombstone);
    void flush_locked();
    void compact_locked();
    void load_tables();

    std::filesystem::path directory_;
    Options options_;
    mutable std::shared_mutex mutex_;
    SkipList memtable_;
    WriteAheadLog wal_;
    std::vector<std::shared_ptr<SSTable>> tables_;
    std::uint64_t sequence_{};
    std::uint64_t next_table_id_{1};
    mutable std::atomic<std::uint64_t> writes_{};
    mutable std::atomic<std::uint64_t> reads_{};
    mutable std::atomic<std::uint64_t> bloom_filter_negatives_{};
};

}  // namespace chronicle
