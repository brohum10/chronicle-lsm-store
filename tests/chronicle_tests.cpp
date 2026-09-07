#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "chronicle/bloom_filter.hpp"
#include "chronicle/database.hpp"
#include "chronicle/skip_list.hpp"
#include "chronicle/sstable.hpp"
#include "chronicle/wal.hpp"

namespace {

#define REQUIRE(condition)                                                                          \
    do {                                                                                            \
        if (!(condition)) {                                                                         \
            throw std::runtime_error(                                                               \
                std::string("requirement failed: ") + #condition + " at line " +                  \
                std::to_string(__LINE__));                                                          \
        }                                                                                           \
    } while (false)

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        static std::atomic<std::uint64_t> next{};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("chronicle-test-" + std::to_string(stamp) + "-" + std::to_string(next++));
        std::filesystem::create_directories(path_);
    }
    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

chronicle::Options fast_options(std::size_t memtable_limit = 64, std::size_t compact_at = 4) {
    return {
        .memtable_max_entries = memtable_limit,
        .compaction_trigger = compact_at,
        .max_key_bytes = 4 * 1024,
        .max_value_bytes = 4 * 1024 * 1024,
        .sync_writes = false,
    };
}

void skip_list_orders_and_updates_entries() {
    chronicle::SkipList list(7);
    list.upsert({1, false, "charlie", "3"});
    list.upsert({2, false, "alpha", "1"});
    list.upsert({3, false, "bravo", "2"});
    list.upsert({4, false, "bravo", "updated"});
    list.upsert({1, false, "bravo", "stale"});
    REQUIRE(list.size() == 3);
    REQUIRE(list.get("bravo")->value == "updated");
    const auto entries = list.entries();
    REQUIRE(entries[0].key == "alpha");
    REQUIRE(entries[1].key == "bravo");
    REQUIRE(entries[2].key == "charlie");
}

void bloom_filter_has_no_false_negatives() {
    chronicle::BloomFilter filter(1'000, 0.01);
    for (int index = 0; index < 1'000; ++index) filter.add("present-" + std::to_string(index));
    for (int index = 0; index < 1'000; ++index) {
        REQUIRE(filter.may_contain("present-" + std::to_string(index)));
    }
    int false_positives = 0;
    for (int index = 0; index < 10'000; ++index) {
        false_positives += filter.may_contain("absent-" + std::to_string(index)) ? 1 : 0;
    }
    REQUIRE(false_positives < 300);
}

void wal_recovers_complete_records_and_ignores_torn_tail() {
    TemporaryDirectory directory;
    const auto path = directory.path() / "wal.log";
    {
        chronicle::WriteAheadLog wal(path, false);
        wal.append({1, false, "alpha", "one"});
        wal.append({2, false, "beta", "two"});
    }
    const auto full_size = std::filesystem::file_size(path);
    std::filesystem::resize_file(path, full_size - 3);
    chronicle::WriteAheadLog recovered(path, false);
    const auto entries = recovered.replay();
    REQUIRE(entries.size() == 1);
    REQUIRE(entries.front().key == "alpha");

    const auto corrupt_path = directory.path() / "corrupt.log";
    {
        chronicle::WriteAheadLog wal(corrupt_path, false);
        wal.append({1, false, "key", "value"});
    }
    std::fstream corrupt(corrupt_path, std::ios::binary | std::ios::in | std::ios::out);
    corrupt.seekg(-1, std::ios::end);
    char byte{};
    corrupt.read(&byte, 1);
    byte ^= static_cast<char>(0xFF);
    corrupt.seekp(-1, std::ios::end);
    corrupt.write(&byte, 1);
    corrupt.close();
    bool rejected = false;
    try {
        chronicle::WriteAheadLog damaged(corrupt_path, false);
        (void)damaged.replay();
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    REQUIRE(rejected);
}

void sstable_round_trips_and_detects_corruption() {
    TemporaryDirectory directory;
    const auto table_path = directory.path() / "sst-1.sst";
    const std::vector<chronicle::Entry> entries{
        {1, false, "alpha", "one"},
        {3, true, "beta", ""},
        {2, false, "gamma", "three"},
    };
    const auto table = chronicle::SSTable::create(table_path, entries);
    REQUIRE(table->size() == 3);
    REQUIRE(table->get("alpha")->value == "one");
    REQUIRE(table->get("beta")->tombstone);
    REQUIRE(!table->get("missing"));
    REQUIRE(chronicle::SSTable::open(table_path)->entries() == entries);

    std::fstream corrupt(table_path, std::ios::binary | std::ios::in | std::ios::out);
    corrupt.seekg(-1, std::ios::end);
    char byte{};
    corrupt.read(&byte, 1);
    byte ^= static_cast<char>(0xFF);
    corrupt.seekp(-1, std::ios::end);
    corrupt.write(&byte, 1);
    corrupt.close();
    bool rejected = false;
    try {
        (void)chronicle::SSTable::open(table_path);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    REQUIRE(rejected);
}

void database_supports_crud_and_validation() {
    TemporaryDirectory directory;
    chronicle::Database database(directory.path(), fast_options());
    database.put("language", "C++20");
    REQUIRE(database.get("language") == "C++20");
    database.put("language", "modern C++");
    REQUIRE(database.get("language") == "modern C++");
    database.erase("language");
    REQUIRE(!database.get("language"));
    bool rejected = false;
    try {
        database.put("", "value");
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    REQUIRE(rejected);
}

void database_recovers_unflushed_wal() {
    TemporaryDirectory directory;
    {
        chronicle::Database database(directory.path(), fast_options(100));
        database.put("unflushed", "survives restart");
        database.put("deleted", "old");
        database.erase("deleted");
    }
    chronicle::Database recovered(directory.path(), fast_options(100));
    REQUIRE(recovered.get("unflushed") == "survives restart");
    REQUIRE(!recovered.get("deleted"));
    REQUIRE(recovered.stats().sequence == 3);
}

void flush_compaction_and_restart_preserve_latest_state() {
    TemporaryDirectory directory;
    {
        chronicle::Database database(directory.path(), fast_options(2, 3));
        database.put("alpha", "v1");
        database.put("beta", "v1");
        database.put("alpha", "v2");
        database.put("gamma", "v1");
        database.erase("beta");
        database.put("delta", "v1");
        database.compact();
        REQUIRE(database.stats().sstable_count == 1);
    }
    chronicle::Database recovered(directory.path(), fast_options(2, 3));
    REQUIRE(recovered.get("alpha") == "v2");
    REQUIRE(!recovered.get("beta"));
    REQUIRE(recovered.get("gamma") == "v1");
    REQUIRE(recovered.get("delta") == "v1");
}

void randomized_workload_matches_reference_model() {
    TemporaryDirectory directory;
    std::map<std::string, std::string> model;
    std::mt19937 random(42);
    {
        chronicle::Database database(directory.path(), fast_options(31, 4));
        for (int operation = 0; operation < 5'000; ++operation) {
            const auto key = "key-" + std::to_string(random() % 250);
            if (random() % 5 == 0) {
                database.erase(key);
                model.erase(key);
            } else {
                const auto value = "value-" + std::to_string(random());
                database.put(key, value);
                model[key] = value;
            }
            if (operation % 173 == 0) database.flush();
            if (operation % 431 == 0) database.compact();
        }
    }
    chronicle::Database recovered(directory.path(), fast_options(31, 4));
    for (int index = 0; index < 250; ++index) {
        const auto key = "key-" + std::to_string(index);
        const auto actual = recovered.get(key);
        const auto expected = model.find(key);
        REQUIRE((expected == model.end() && !actual) ||
                (expected != model.end() && actual && *actual == expected->second));
    }
}

void concurrent_writers_do_not_lose_updates() {
    TemporaryDirectory directory;
    chronicle::Database database(directory.path(), fast_options(128, 5));
    std::vector<std::thread> writers;
    for (int worker = 0; worker < 4; ++worker) {
        writers.emplace_back([worker, &database] {
            for (int item = 0; item < 250; ++item) {
                database.put(
                    "worker-" + std::to_string(worker) + "-" + std::to_string(item),
                    "value-" + std::to_string(item));
            }
        });
    }
    for (auto& writer : writers) writer.join();
    for (int worker = 0; worker < 4; ++worker) {
        for (int item = 0; item < 250; ++item) {
            REQUIRE(database.get("worker-" + std::to_string(worker) + "-" + std::to_string(item)) ==
                    "value-" + std::to_string(item));
        }
    }
    REQUIRE(database.stats().writes == 1'000);
}

}  // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests{
        {"skip list ordering and updates", skip_list_orders_and_updates_entries},
        {"Bloom filter accuracy", bloom_filter_has_no_false_negatives},
        {"WAL torn-write recovery", wal_recovers_complete_records_and_ignores_torn_tail},
        {"SSTable round trip and checksums", sstable_round_trips_and_detects_corruption},
        {"database CRUD and validation", database_supports_crud_and_validation},
        {"database WAL recovery", database_recovers_unflushed_wal},
        {"flush, compaction, and restart", flush_compaction_and_restart_preserve_latest_state},
        {"randomized reference-model workload", randomized_workload_matches_reference_model},
        {"concurrent writers", concurrent_writers_do_not_lose_updates},
    };
    std::size_t passed = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            ++passed;
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
        }
    }
    std::cout << passed << '/' << tests.size() << " tests passed\n";
    return passed == tests.size() ? 0 : 1;
}
