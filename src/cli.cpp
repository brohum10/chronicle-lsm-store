#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>

#include "chronicle/database.hpp"

namespace {

void print_help() {
    std::cout << "Commands:\n"
                 "  put <key> <value>   Store or replace a value\n"
                 "  get <key>           Read a value\n"
                 "  delete <key>        Write a deletion marker\n"
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
                    database.put(std::move(key), std::move(value));
                    std::cout << "ok\n";
                } else if (command == "get") {
                    std::string key;
                    input >> key;
                    const auto value = database.get(key);
                    std::cout << (value ? *value : "(not found)") << '\n';
                } else if (command == "delete") {
                    std::string key;
                    input >> key;
                    database.erase(std::move(key));
                    std::cout << "ok\n";
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
                              << " bloom_negatives=" << stats.bloom_filter_negatives << '\n';
                } else if (command == "help") {
                    print_help();
                } else if (command == "quit" || command == "exit") {
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
