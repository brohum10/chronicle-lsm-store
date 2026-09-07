#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace chronicle {

std::uint32_t crc32(std::span<const std::byte> bytes);

}  // namespace chronicle
