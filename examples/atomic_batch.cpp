#include <iostream>

#include "chronicle/database.hpp"

int main() {
    chronicle::Database database("./example-data");
    database.write_batch({
        {"user:1001:name", "Ada"},
        {"user:1001:role", "engineer"},
        {"user:1001:temporary-token", std::nullopt},
    });

    for (const auto& item : database.scan_prefix("user:1001:")) {
        std::cout << item.key << " = " << item.value << '\n';
    }
}
