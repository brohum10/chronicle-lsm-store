#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "chronicle/entry.hpp"

namespace chronicle::codec {

template <typename Integer>
void append_integer(std::vector<std::byte>& output, Integer value) {
    for (std::size_t byte = 0; byte < sizeof(Integer); ++byte) {
        output.push_back(static_cast<std::byte>((value >> (byte * 8U)) & 0xFFU));
    }
}

template <typename Integer>
Integer read_integer(std::span<const std::byte> input, std::size_t& offset) {
    if (input.size() - std::min(input.size(), offset) < sizeof(Integer)) {
        throw std::runtime_error("truncated integer in storage record");
    }
    Integer result{};
    for (std::size_t byte = 0; byte < sizeof(Integer); ++byte) {
        result |= static_cast<Integer>(std::to_integer<unsigned char>(input[offset + byte]))
                  << (byte * 8U);
    }
    offset += sizeof(Integer);
    return result;
}

inline std::vector<std::byte> encode_entry(const Entry& entry) {
    if (entry.key.size() > UINT32_MAX || entry.value.size() > UINT32_MAX) {
        throw std::length_error("entry is too large to encode");
    }
    if (entry.key.size() + static_cast<std::uint64_t>(entry.value.size()) > UINT32_MAX - 17U) {
        throw std::length_error("encoded entry exceeds the record format limit");
    }
    std::vector<std::byte> payload;
    payload.reserve(17 + entry.key.size() + entry.value.size());
    append_integer<std::uint8_t>(payload, entry.tombstone ? 1U : 0U);
    append_integer<std::uint64_t>(payload, entry.sequence);
    append_integer<std::uint32_t>(payload, static_cast<std::uint32_t>(entry.key.size()));
    append_integer<std::uint32_t>(payload, static_cast<std::uint32_t>(entry.value.size()));
    for (const char character : entry.key) payload.push_back(static_cast<std::byte>(character));
    for (const char character : entry.value) payload.push_back(static_cast<std::byte>(character));
    return payload;
}

inline Entry decode_entry(std::span<const std::byte> payload) {
    std::size_t offset = 0;
    Entry result;
    const auto tombstone = read_integer<std::uint8_t>(payload, offset);
    if (tombstone > 1U) throw std::runtime_error("invalid tombstone marker");
    result.tombstone = tombstone == 1U;
    result.sequence = read_integer<std::uint64_t>(payload, offset);
    const auto key_size = read_integer<std::uint32_t>(payload, offset);
    const auto value_size = read_integer<std::uint32_t>(payload, offset);
    const auto remaining = payload.size() - std::min(payload.size(), offset);
    if (static_cast<std::uint64_t>(key_size) + value_size != remaining) {
        throw std::runtime_error("invalid entry payload length");
    }
    result.key.reserve(key_size);
    result.value.reserve(value_size);
    for (std::size_t index = 0; index < key_size; ++index) {
        result.key.push_back(static_cast<char>(payload[offset + index]));
    }
    offset += key_size;
    for (std::size_t index = 0; index < value_size; ++index) {
        result.value.push_back(static_cast<char>(payload[offset + index]));
    }
    return result;
}

}  // namespace chronicle::codec
