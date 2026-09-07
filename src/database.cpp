#include "chronicle/database.hpp"

#include <algorithm>
#include <iomanip>
#include <map>
#include <mutex>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace chronicle {
namespace {

std::string table_filename(std::uint64_t identifier) {
    std::ostringstream name;
    name << "sst-" << std::setw(20) << std::setfill('0') << identifier << ".sst";
    return name.str();
}

}  // namespace

Database::Database(std::filesystem::path directory, Options options)
    : directory_(std::move(directory)),
      options_(options),
      wal_(directory_ / "wal.log", options_.sync_writes) {
    if (options_.memtable_max_entries == 0) throw std::invalid_argument("memtable limit must be positive");
    if (options_.compaction_trigger < 2) throw std::invalid_argument("compaction trigger must be at least two");
    if (options_.max_key_bytes == 0 || options_.max_value_bytes == 0) {
        throw std::invalid_argument("key and value limits must be positive");
    }
    load_tables();
    for (auto& entry : wal_.replay()) {
        sequence_ = std::max(sequence_, entry.sequence);
        memtable_.upsert(std::move(entry));
    }
}

Database::~Database() = default;

void Database::put(std::string key, std::string value) {
    validate(key, value);
    apply_write(std::move(key), std::move(value), false);
}

void Database::erase(std::string key) {
    validate(key, {});
    apply_write(std::move(key), {}, true);
}

std::optional<std::string> Database::get(std::string_view key) const {
    if (key.empty()) throw std::invalid_argument("key cannot be empty");
    if (key.size() > options_.max_key_bytes) throw std::length_error("key exceeds configured limit");
    reads_.fetch_add(1, std::memory_order_relaxed);
    std::shared_lock lock(mutex_);
    if (const auto entry = memtable_.get(key)) {
        if (entry->tombstone) return std::nullopt;
        return entry->value;
    }
    for (const auto& table : tables_) {
        if (!table->may_contain(key)) {
            bloom_filter_negatives_.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        if (const auto entry = table->get(key)) {
            if (entry->tombstone) return std::nullopt;
            return entry->value;
        }
    }
    return std::nullopt;
}

void Database::flush() {
    std::unique_lock lock(mutex_);
    flush_locked();
}

void Database::compact() {
    std::unique_lock lock(mutex_);
    if (memtable_.size() != 0) flush_locked();
    compact_locked();
}

Stats Database::stats() const {
    std::shared_lock lock(mutex_);
    return {
        .memtable_entries = memtable_.size(),
        .sstable_count = tables_.size(),
        .sequence = sequence_,
        .writes = writes_.load(std::memory_order_relaxed),
        .reads = reads_.load(std::memory_order_relaxed),
        .bloom_filter_negatives = bloom_filter_negatives_.load(std::memory_order_relaxed),
    };
}

void Database::validate(std::string_view key, std::string_view value) const {
    if (key.empty()) throw std::invalid_argument("key cannot be empty");
    if (key.size() > options_.max_key_bytes) throw std::length_error("key exceeds configured limit");
    if (value.size() > options_.max_value_bytes) throw std::length_error("value exceeds configured limit");
}

void Database::apply_write(std::string key, std::string value, bool tombstone) {
    std::unique_lock lock(mutex_);
    Entry entry{++sequence_, tombstone, std::move(key), std::move(value)};
    wal_.append(entry);
    memtable_.upsert(std::move(entry));
    writes_.fetch_add(1, std::memory_order_relaxed);
    if (memtable_.size() >= options_.memtable_max_entries) flush_locked();
}

void Database::flush_locked() {
    if (memtable_.size() == 0) return;
    auto table = SSTable::create(directory_ / table_filename(next_table_id_++), memtable_.entries());
    tables_.insert(tables_.begin(), std::move(table));
    wal_.reset();
    memtable_.clear();
    if (tables_.size() >= options_.compaction_trigger) compact_locked();
}

void Database::compact_locked() {
    if (tables_.size() < 2) return;

    std::map<std::string, Entry, std::less<>> latest;
    for (const auto& table : tables_) {
        for (auto& entry : table->entries()) {
            const auto existing = latest.find(entry.key);
            if (existing == latest.end() || entry.sequence > existing->second.sequence) {
                latest.insert_or_assign(entry.key, std::move(entry));
            }
        }
    }
    std::vector<Entry> merged;
    merged.reserve(latest.size());
    for (auto& [key, entry] : latest) merged.push_back(std::move(entry));

    auto replacement = SSTable::create(directory_ / table_filename(next_table_id_++), std::move(merged));
    const auto previous_tables = tables_;
    tables_.assign(1, std::move(replacement));
    for (const auto& table : previous_tables) {
        std::error_code error;
        std::filesystem::remove(table->path(), error);
        if (error) throw std::system_error(error, "unable to remove compacted SSTable");
    }
}

void Database::load_tables() {
    std::filesystem::create_directories(directory_);
    static const std::regex table_pattern(R"(^sst-([0-9]+)\.sst$)");
    std::vector<std::pair<std::uint64_t, std::filesystem::path>> discovered;
    for (const auto& item : std::filesystem::directory_iterator(directory_)) {
        if (!item.is_regular_file()) continue;
        std::smatch match;
        const auto filename = item.path().filename().string();
        if (!std::regex_match(filename, match, table_pattern)) continue;
        const auto identifier = std::stoull(match[1].str());
        discovered.emplace_back(identifier, item.path());
        next_table_id_ = std::max(next_table_id_, identifier + 1);
    }
    std::sort(discovered.begin(), discovered.end(), [](const auto& left, const auto& right) {
        return left.first > right.first;
    });
    for (const auto& [identifier, path] : discovered) {
        (void)identifier;
        auto table = SSTable::open(path);
        sequence_ = std::max(sequence_, table->max_sequence());
        tables_.push_back(std::move(table));
    }
}

}  // namespace chronicle
