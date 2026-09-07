#include "chronicle/database.hpp"

#include <algorithm>
#include <iomanip>
#include <map>
#include <mutex>
#include <queue>
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
    apply_write(std::move(key), std::move(value), false);
}

void Database::erase(std::string key) {
    apply_write(std::move(key), {}, true);
}

void Database::write_batch(std::vector<Mutation> mutations) {
    if (mutations.empty()) return;
    for (const auto& mutation : mutations) {
        validate(mutation.key, mutation.value ? std::string_view(*mutation.value) : std::string_view{});
    }

    std::unique_lock lock(mutex_);
    if (mutations.size() > std::numeric_limits<std::uint64_t>::max() - sequence_) {
        throw std::overflow_error("sequence number space exhausted");
    }
    std::vector<Entry> entries;
    entries.reserve(mutations.size());
    for (auto& mutation : mutations) {
        entries.push_back({
            ++sequence_,
            !mutation.value.has_value(),
            std::move(mutation.key),
            mutation.value ? std::move(*mutation.value) : std::string{},
        });
    }
    wal_.append_batch(entries);
    for (auto& entry : entries) memtable_.upsert(std::move(entry));
    writes_.fetch_add(mutations.size(), std::memory_order_relaxed);
    if (memtable_.size() >= options_.memtable_max_entries) flush_locked();
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

std::vector<KeyValue> Database::scan(
    std::string_view start_inclusive,
    std::string_view end_exclusive,
    std::size_t limit) const {
    if (start_inclusive.size() > options_.max_key_bytes || end_exclusive.size() > options_.max_key_bytes) {
        throw std::length_error("range boundary exceeds configured key limit");
    }
    range_scans_.fetch_add(1, std::memory_order_relaxed);
    std::shared_lock lock(mutex_);
    auto result = scan_locked(start_inclusive, end_exclusive, limit);
    range_entries_returned_.fetch_add(result.size(), std::memory_order_relaxed);
    return result;
}

std::vector<KeyValue> Database::scan_prefix(std::string_view prefix, std::size_t limit) const {
    if (prefix.size() > options_.max_key_bytes) throw std::length_error("prefix exceeds configured key limit");
    range_scans_.fetch_add(1, std::memory_order_relaxed);
    std::shared_lock lock(mutex_);
    auto result = scan_locked(prefix, {}, limit, prefix);
    range_entries_returned_.fetch_add(result.size(), std::memory_order_relaxed);
    return result;
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
        .flushes = flushes_.load(std::memory_order_relaxed),
        .compactions = compactions_.load(std::memory_order_relaxed),
        .range_scans = range_scans_.load(std::memory_order_relaxed),
        .range_entries_returned = range_entries_returned_.load(std::memory_order_relaxed),
    };
}

void Database::validate(std::string_view key, std::string_view value) const {
    if (key.empty()) throw std::invalid_argument("key cannot be empty");
    if (key.size() > options_.max_key_bytes) throw std::length_error("key exceeds configured limit");
    if (value.size() > options_.max_value_bytes) throw std::length_error("value exceeds configured limit");
}

void Database::apply_write(std::string key, std::string value, bool tombstone) {
    std::vector<Mutation> mutations;
    mutations.push_back({std::move(key), tombstone ? std::nullopt : std::optional<std::string>(std::move(value))});
    write_batch(std::move(mutations));
}

void Database::flush_locked() {
    if (memtable_.size() == 0) return;
    auto table = SSTable::create(directory_ / table_filename(next_table_id_++), memtable_.entries());
    tables_.insert(tables_.begin(), std::move(table));
    wal_.reset();
    memtable_.clear();
    flushes_.fetch_add(1, std::memory_order_relaxed);
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
    compactions_.fetch_add(1, std::memory_order_relaxed);
}

std::vector<KeyValue> Database::scan_locked(
    std::string_view start_inclusive,
    std::string_view end_exclusive,
    std::size_t limit,
    std::string_view required_prefix) const {
    if (limit == 0 || (!end_exclusive.empty() && start_inclusive >= end_exclusive)) return {};

    struct Source {
        std::vector<Entry> memory;
        std::shared_ptr<SSTable> table;
        std::size_t index{};
    };
    std::vector<Source> sources;
    sources.reserve(tables_.size() + 1);
    sources.push_back({memtable_.entries(), nullptr, 0});
    for (const auto& table : tables_) {
        sources.push_back({{}, table, table->lower_bound_index(start_inclusive)});
    }

    const auto source_size = [&sources](std::size_t source) {
        return sources[source].table ? sources[source].table->size() : sources[source].memory.size();
    };
    const auto source_key = [&sources](std::size_t source, std::size_t index) -> const std::string& {
        return sources[source].table ? sources[source].table->key_at(index)
                                     : sources[source].memory[index].key;
    };
    const auto source_entry = [&sources](std::size_t source, std::size_t index) {
        return sources[source].table ? sources[source].table->entry_at(index)
                                     : sources[source].memory[index];
    };

    struct Cursor {
        std::size_t source;
        std::size_t index;
    };
    const auto compare = [&source_key](const Cursor& left, const Cursor& right) {
        return source_key(left.source, left.index) > source_key(right.source, right.index);
    };
    std::priority_queue<Cursor, std::vector<Cursor>, decltype(compare)> heap(compare);
    for (std::size_t source = 0; source < sources.size(); ++source) {
        if (!sources[source].table) {
            const auto first = std::lower_bound(
                sources[source].memory.begin(), sources[source].memory.end(), start_inclusive,
                [](const Entry& entry, std::string_view key) { return entry.key < key; });
            sources[source].index = static_cast<std::size_t>(first - sources[source].memory.begin());
        }
        if (sources[source].index < source_size(source) &&
            (end_exclusive.empty() || source_key(source, sources[source].index) < end_exclusive)) {
            heap.push({source, sources[source].index});
        }
    }

    std::vector<KeyValue> result;
    result.reserve(std::min<std::size_t>(limit, 256));
    while (!heap.empty() && result.size() < limit) {
        const auto key = source_key(heap.top().source, heap.top().index);
        if (!end_exclusive.empty() && key >= end_exclusive) break;
        if (!required_prefix.empty() && !key.starts_with(required_prefix)) break;

        std::optional<Entry> newest;
        while (!heap.empty() && source_key(heap.top().source, heap.top().index) == key) {
            const auto cursor = heap.top();
            heap.pop();
            const auto candidate = source_entry(cursor.source, cursor.index);
            if (!newest || candidate.sequence > newest->sequence) newest = candidate;

            const auto next = cursor.index + 1;
            if (next < source_size(cursor.source) &&
                (end_exclusive.empty() || source_key(cursor.source, next) < end_exclusive)) {
                heap.push({cursor.source, next});
            }
        }
        if (newest && !newest->tombstone) result.push_back({newest->key, newest->value});
    }
    return result;
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
