#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <limits>
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
    std::uint64_t flushes{};
    std::uint64_t compactions{};
    std::uint64_t range_scans{};
    std::uint64_t range_entries_returned{};
};

struct Mutation {
    std::string key;
    std::optional<std::string> value;
};

struct KeyValue {
    std::string key;
    std::string value;
};

class Database {
public:
    explicit Database(std::filesystem::path directory, Options options = {});
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    void put(std::string key, std::string value);
    void erase(std::string key);
    void write_batch(std::vector<Mutation> mutations);
    [[nodiscard]] std::optional<std::string> get(std::string_view key) const;
    [[nodiscard]] std::vector<KeyValue> scan(
        std::string_view start_inclusive,
        std::string_view end_exclusive = {},
        std::size_t limit = std::numeric_limits<std::size_t>::max()) const;
    [[nodiscard]] std::vector<KeyValue> scan_prefix(
        std::string_view prefix,
        std::size_t limit = std::numeric_limits<std::size_t>::max()) const;
    void flush();
    void compact();
    [[nodiscard]] Stats stats() const;

private:
    void validate(std::string_view key, std::string_view value) const;
    void apply_write(std::string key, std::string value, bool tombstone);
    void flush_locked();
    void compact_locked();
    void load_tables();
    [[nodiscard]] std::vector<KeyValue> scan_locked(
        std::string_view start_inclusive,
        std::string_view end_exclusive,
        std::size_t limit,
        std::string_view required_prefix = {}) const;

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
    mutable std::atomic<std::uint64_t> flushes_{};
    mutable std::atomic<std::uint64_t> compactions_{};
    mutable std::atomic<std::uint64_t> range_scans_{};
    mutable std::atomic<std::uint64_t> range_entries_returned_{};
};

}  // namespace chronicle
