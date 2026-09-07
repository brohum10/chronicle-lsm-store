#include "chronicle/crc32.hpp"

#include <array>

namespace chronicle {
namespace {

constexpr auto make_table() {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t index = 0; index < table.size(); ++index) {
        auto value = index;
        for (int bit = 0; bit < 8; ++bit) {
            value = (value & 1U) ? (value >> 1U) ^ 0xEDB88320U : value >> 1U;
        }
        table[index] = value;
    }
    return table;
}

constexpr auto kTable = make_table();

}  // namespace

std::uint32_t crc32(std::span<const std::byte> bytes) {
    std::uint32_t checksum = 0xFFFFFFFFU;
    for (const auto byte : bytes) {
        const auto index = (checksum ^ std::to_integer<std::uint8_t>(byte)) & 0xFFU;
        checksum = (checksum >> 8U) ^ kTable[index];
    }
    return checksum ^ 0xFFFFFFFFU;
}

}  // namespace chronicle
