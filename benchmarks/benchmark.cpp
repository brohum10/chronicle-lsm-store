#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>
#include <unistd.h>

#include "chronicle/database.hpp"

namespace {

using Clock = std::chrono::steady_clock;

double seconds_since(Clock::time_point started) {
    return std::chrono::duration<double>(Clock::now() - started).count();
}

std::string key_for(std::uint64_t index) {
    std::ostringstream key;
    key << "key-" << std::setw(8) << std::setfill('0') << index;
    return key.str();
}

}  // namespace

int main() {
    constexpr std::uint64_t kOperations = 100'000;
    constexpr std::uint64_t kDurableOperations = 2'000;
    constexpr std::uint64_t kRangeScans = 1'000;
    constexpr std::size_t kBatchSize = 100;
    const auto directory = std::filesystem::temp_directory_path() /
                           ("chronicle-benchmark-" + std::to_string(::getpid()));
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);

    try {
        chronicle::Options options{
            .memtable_max_entries = 4'096,
            .compaction_trigger = 8,
            .max_key_bytes = 4 * 1'024,
            .max_value_bytes = 4 * 1'024 * 1'024,
            .sync_writes = false,
        };
        chronicle::Database database(directory / "individual", options);
        const std::string value(128, 'x');

        const auto writes_started = Clock::now();
        for (std::uint64_t index = 0; index < kOperations; ++index) {
            database.put(key_for(index), value);
        }
        database.flush();
        const auto write_seconds = seconds_since(writes_started);

        std::mt19937_64 random(20260907);
        std::uint64_t checksum = 0;
        const auto reads_started = Clock::now();
        for (std::uint64_t index = 0; index < kOperations; ++index) {
            const auto result = database.get(key_for(random() % kOperations));
            checksum += result ? result->size() : 0;
        }
        const auto read_seconds = seconds_since(reads_started);

        std::uint64_t range_checksum = 0;
        const auto ranges_started = Clock::now();
        for (std::uint64_t index = 0; index < kRangeScans; ++index) {
            const auto start = random() % (kOperations - 100);
            for (const auto& item : database.scan(key_for(start), {}, 100)) {
                range_checksum += item.key.size() + item.value.size();
            }
        }
        const auto range_seconds = seconds_since(ranges_started);

        const auto compact_started = Clock::now();
        database.compact();
        const auto compact_seconds = seconds_since(compact_started);
        const auto stats = database.stats();

        chronicle::Database batched_database(directory / "batched", options);
        const auto batches_started = Clock::now();
        for (std::uint64_t first = 0; first < kOperations; first += kBatchSize) {
            std::vector<chronicle::Mutation> batch;
            batch.reserve(kBatchSize);
            for (std::uint64_t offset = 0; offset < kBatchSize; ++offset) {
                batch.push_back({key_for(first + offset), value});
            }
            batched_database.write_batch(std::move(batch));
        }
        batched_database.flush();
        const auto batch_seconds = seconds_since(batches_started);

        auto durable_options = options;
        durable_options.memtable_max_entries = 4'096;
        durable_options.sync_writes = true;
        chronicle::Database durable_individual(directory / "durable-individual", durable_options);
        const auto durable_writes_started = Clock::now();
        for (std::uint64_t index = 0; index < kDurableOperations; ++index) {
            durable_individual.put(key_for(index), value);
        }
        durable_individual.flush();
        const auto durable_write_seconds = seconds_since(durable_writes_started);

        chronicle::Database durable_batched(directory / "durable-batched", durable_options);
        const auto durable_batches_started = Clock::now();
        for (std::uint64_t first = 0; first < kDurableOperations; first += kBatchSize) {
            std::vector<chronicle::Mutation> batch;
            batch.reserve(kBatchSize);
            for (std::uint64_t offset = 0; offset < kBatchSize; ++offset) {
                batch.push_back({key_for(first + offset), value});
            }
            durable_batched.write_batch(std::move(batch));
        }
        durable_batched.flush();
        const auto durable_batch_seconds = seconds_since(durable_batches_started);

        std::cout << std::fixed << std::setprecision(0)
                  << "{\n"
                  << "  \"operations\": " << kOperations << ",\n"
                  << "  \"durable_operations\": " << kDurableOperations << ",\n"
                  << "  \"value_bytes\": " << value.size() << ",\n"
                  << "  \"batch_size\": " << kBatchSize << ",\n"
                  << "  \"write_ops_per_second\": " << kOperations / write_seconds << ",\n"
                  << "  \"batched_write_ops_per_second\": " << kOperations / batch_seconds << ",\n"
                  << "  \"fsync_write_ops_per_second\": "
                  << kDurableOperations / durable_write_seconds << ",\n"
                  << "  \"fsync_batched_write_ops_per_second\": "
                  << kDurableOperations / durable_batch_seconds << ",\n"
                  << "  \"random_read_ops_per_second\": " << kOperations / read_seconds << ",\n"
                  << "  \"range_queries_per_second\": " << kRangeScans / range_seconds << ",\n"
                  << std::setprecision(3)
                  << "  \"full_compaction_seconds\": " << compact_seconds << ",\n"
                  << "  \"sstables_after_compaction\": " << stats.sstable_count << ",\n"
                  << "  \"read_checksum\": " << checksum << ",\n"
                  << "  \"range_checksum\": " << range_checksum << "\n"
                  << "}\n";
    } catch (const std::exception& error) {
        std::cerr << "benchmark failed: " << error.what() << '\n';
        std::filesystem::remove_all(directory, ignored);
        return 1;
    }
    std::filesystem::remove_all(directory, ignored);
    return 0;
}
