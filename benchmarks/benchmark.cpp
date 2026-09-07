#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <unistd.h>

#include "chronicle/database.hpp"

namespace {

using Clock = std::chrono::steady_clock;

double seconds_since(Clock::time_point started) {
    return std::chrono::duration<double>(Clock::now() - started).count();
}

}  // namespace

int main() {
    constexpr std::uint64_t kOperations = 100'000;
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
        chronicle::Database database(directory, options);
        const std::string value(128, 'x');

        const auto writes_started = Clock::now();
        for (std::uint64_t index = 0; index < kOperations; ++index) {
            database.put("key-" + std::to_string(index), value);
        }
        database.flush();
        const auto write_seconds = seconds_since(writes_started);

        std::mt19937_64 random(20260907);
        std::uint64_t checksum = 0;
        const auto reads_started = Clock::now();
        for (std::uint64_t index = 0; index < kOperations; ++index) {
            const auto result = database.get("key-" + std::to_string(random() % kOperations));
            checksum += result ? result->size() : 0;
        }
        const auto read_seconds = seconds_since(reads_started);

        const auto compact_started = Clock::now();
        database.compact();
        const auto compact_seconds = seconds_since(compact_started);
        const auto stats = database.stats();

        std::cout << std::fixed << std::setprecision(0)
                  << "{\n"
                  << "  \"operations\": " << kOperations << ",\n"
                  << "  \"value_bytes\": " << value.size() << ",\n"
                  << "  \"write_ops_per_second\": " << kOperations / write_seconds << ",\n"
                  << "  \"random_read_ops_per_second\": " << kOperations / read_seconds << ",\n"
                  << std::setprecision(3)
                  << "  \"full_compaction_seconds\": " << compact_seconds << ",\n"
                  << "  \"sstables_after_compaction\": " << stats.sstable_count << ",\n"
                  << "  \"read_checksum\": " << checksum << "\n"
                  << "}\n";
    } catch (const std::exception& error) {
        std::cerr << "benchmark failed: " << error.what() << '\n';
        std::filesystem::remove_all(directory, ignored);
        return 1;
    }
    std::filesystem::remove_all(directory, ignored);
    return 0;
}
