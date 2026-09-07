#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "chronicle/bloom_filter.hpp"
#include "chronicle/entry.hpp"

namespace chronicle {

class Database;

class SSTable {
public:
    ~SSTable();
    SSTable(const SSTable&) = delete;
    SSTable& operator=(const SSTable&) = delete;

    static std::shared_ptr<SSTable> create(
        const std::filesystem::path& final_path,
        std::vector<Entry> sorted_entries);
    static std::shared_ptr<SSTable> open(const std::filesystem::path& path);

    [[nodiscard]] std::optional<Entry> get(std::string_view key) const;
    [[nodiscard]] bool may_contain(std::string_view key) const { return filter_.may_contain(key); }
    [[nodiscard]] std::vector<Entry> entries() const;
    [[nodiscard]] std::vector<Entry> scan(
        std::string_view start_inclusive,
        std::string_view end_exclusive = {},
        std::size_t limit = std::numeric_limits<std::size_t>::max()) const;
    [[nodiscard]] std::uint64_t max_sequence() const noexcept { return max_sequence_; }
    [[nodiscard]] std::size_t size() const noexcept { return index_.size(); }
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    friend class Database;

    struct IndexEntry {
        std::string key;
        std::uint64_t offset;
    };

    SSTable(
        std::filesystem::path path,
        BloomFilter filter,
        std::vector<IndexEntry> index,
        std::uint64_t max_sequence,
        int descriptor);

    [[nodiscard]] Entry read_entry(std::uint64_t offset) const;
    [[nodiscard]] std::size_t lower_bound_index(std::string_view key) const;
    [[nodiscard]] const std::string& key_at(std::size_t index) const;
    [[nodiscard]] Entry entry_at(std::size_t index) const;

    std::filesystem::path path_;
    BloomFilter filter_;
    std::vector<IndexEntry> index_;
    std::uint64_t max_sequence_{};
    int descriptor_{-1};
};

}  // namespace chronicle
