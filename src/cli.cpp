#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "chronicle/database.hpp"

namespace {

void print_help() {
    std::cout << "Commands:\n"
                 "  put <key> <value>   Store or replace a value\n"
                 "  get <key>           Read a value\n"
                 "  delete <key>        Write a deletion marker\n"
                 "  scan <from> <to> [limit]  Read a sorted half-open key range\n"
                 "  prefix <text> [limit]     Read keys sharing a prefix\n"
                 "  begin / commit / abort    Control an atomic write batch\n"
                 "  flush               Persist the in-memory table\n"
                 "  compact             Merge immutable tables\n"
                 "  stats               Show engine counters\n"
                 "  help                Show this message\n"
                 "  quit                Exit Chronicle\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: chronicle_cli <database-directory>\n";
        return 2;
    }
    try {
        chronicle::Database database{std::filesystem::path(argv[1])};
        std::cout << "Chronicle opened at " << argv[1] << "\n";
        print_help();

        std::string line;
        std::vector<chronicle::Mutation> pending_batch;
        bool batching = false;
        while (std::cout << "chronicle> " && std::getline(std::cin, line)) {
            std::istringstream input(line);
            std::string command;
            input >> command;
            if (command.empty()) continue;
            try {
                if (command == "put") {
                    std::string key;
                    input >> key;
                    input >> std::ws;
                    std::string value;
                    std::getline(input, value);
                    if (batching) {
                        pending_batch.push_back({std::move(key), std::move(value)});
                        std::cout << "queued\n";
                    } else {
                        database.put(std::move(key), std::move(value));
                        std::cout << "ok\n";
                    }
                } else if (command == "get") {
                    std::string key;
                    input >> key;
                    const auto value = database.get(key);
                    std::cout << (value ? *value : "(not found)") << '\n';
                } else if (command == "delete") {
                    std::string key;
                    input >> key;
                    if (batching) {
                        pending_batch.push_back({std::move(key), std::nullopt});
                        std::cout << "queued\n";
                    } else {
                        database.erase(std::move(key));
                        std::cout << "ok\n";
                    }
                } else if (command == "scan") {
                    std::string start;
                    std::string end;
                    std::size_t limit = 20;
                    input >> start >> end;
                    if (input >> limit) {}
                    for (const auto& item : database.scan(start, end, limit)) {
                        std::cout << item.key << " = " << item.value << '\n';
                    }
                } else if (command == "prefix") {
                    std::string prefix;
                    std::size_t limit = 20;
                    input >> prefix;
                    if (input >> limit) {}
                    for (const auto& item : database.scan_prefix(prefix, limit)) {
                        std::cout << item.key << " = " << item.value << '\n';
                    }
                } else if (command == "begin") {
                    if (batching) throw std::logic_error("a batch is already active");
                    batching = true;
                    pending_batch.clear();
                    std::cout << "batch started\n";
                } else if (command == "commit") {
                    if (!batching) throw std::logic_error("no batch is active");
                    const auto count = pending_batch.size();
                    database.write_batch(std::move(pending_batch));
                    pending_batch.clear();
                    batching = false;
                    std::cout << "committed " << count << " mutations\n";
                } else if (command == "abort") {
                    if (!batching) throw std::logic_error("no batch is active");
                    pending_batch.clear();
                    batching = false;
                    std::cout << "batch discarded\n";
                } else if (command == "flush") {
                    database.flush();
                    std::cout << "flushed\n";
                } else if (command == "compact") {
                    database.compact();
                    std::cout << "compacted\n";
                } else if (command == "stats") {
                    const auto stats = database.stats();
                    std::cout << "sequence=" << stats.sequence
                              << " memtable_entries=" << stats.memtable_entries
                              << " sstables=" << stats.sstable_count << " writes=" << stats.writes
                              << " reads=" << stats.reads
                              << " bloom_negatives=" << stats.bloom_filter_negatives
                              << " flushes=" << stats.flushes
                              << " compactions=" << stats.compactions
                              << " range_scans=" << stats.range_scans
                              << " range_entries=" << stats.range_entries_returned << '\n';
                } else if (command == "help") {
                    print_help();
                } else if (command == "quit" || command == "exit") {
                    if (batching) std::cout << "discarding uncommitted batch\n";
                    break;
                } else {
                    std::cout << "unknown command; type 'help'\n";
                }
            } catch (const std::exception& error) {
                std::cerr << "error: " << error.what() << '\n';
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "fatal: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
