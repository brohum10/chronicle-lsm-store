#include "chronicle/bloom_filter.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace chronicle {

BloomFilter::BloomFilter(std::size_t expected_items, double false_positive_rate) {
    expected_items = std::max<std::size_t>(1, expected_items);
    if (!(false_positive_rate > 0.0 && false_positive_rate < 1.0)) {
        throw std::invalid_argument("false-positive rate must be between zero and one");
    }
    const auto bits = -static_cast<double>(expected_items) * std::log(false_positive_rate) /
                      std::pow(std::log(2.0), 2.0);
    bit_count_ = std::max<std::size_t>(64, static_cast<std::size_t>(std::ceil(bits)));
    bit_count_ = ((bit_count_ + 63) / 64) * 64;
    hash_count_ = std::max<std::size_t>(1, static_cast<std::size_t>(
        std::round((static_cast<double>(bit_count_) / expected_items) * std::log(2.0))));
    words_.assign(bit_count_ / 64, 0);
}

BloomFilter::BloomFilter(
    std::size_t bit_count,
    std::size_t hash_count,
    std::vector<std::uint64_t> words)
    : bit_count_(bit_count), hash_count_(hash_count), words_(std::move(words)) {
    if (bit_count_ == 0 || hash_count_ == 0 || words_.size() * 64 != bit_count_) {
        throw std::invalid_argument("invalid serialized Bloom filter");
    }
}

void BloomFilter::add(std::string_view key) {
    const auto first = hash(key, 0x9E3779B185EBCA87ULL);
    const auto second = hash(key, 0xC2B2AE3D27D4EB4FULL) | 1ULL;
    for (std::size_t index = 0; index < hash_count_; ++index) {
        const auto bit = (first + index * second) % bit_count_;
        words_[bit / 64] |= 1ULL << (bit % 64);
    }
}

bool BloomFilter::may_contain(std::string_view key) const {
    const auto first = hash(key, 0x9E3779B185EBCA87ULL);
    const auto second = hash(key, 0xC2B2AE3D27D4EB4FULL) | 1ULL;
    for (std::size_t index = 0; index < hash_count_; ++index) {
        const auto bit = (first + index * second) % bit_count_;
        if ((words_[bit / 64] & (1ULL << (bit % 64))) == 0) return false;
    }
    return true;
}

std::uint64_t BloomFilter::hash(std::string_view key, std::uint64_t seed) {
    auto value = 1469598103934665603ULL ^ seed;
    for (const unsigned char byte : key) {
        value ^= byte;
        value *= 1099511628211ULL;
    }
    value ^= value >> 33U;
    value *= 0xff51afd7ed558ccdULL;
    value ^= value >> 33U;
    return value;
}

}  // namespace chronicle
