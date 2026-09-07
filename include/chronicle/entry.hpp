#pragma once

#include <cstdint>
#include <string>

namespace chronicle {

struct Entry {
    std::uint64_t sequence{};
    bool tombstone{};
    std::string key;
    std::string value;

    bool operator==(const Entry&) const = default;
};

}  // namespace chronicle
