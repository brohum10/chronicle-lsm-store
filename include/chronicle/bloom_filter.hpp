#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace chronicle {

class BloomFilter {
public:
    explicit BloomFilter(std::size_t expected_items = 1, double false_positive_rate = 0.01);
    BloomFilter(std::size_t bit_count, std::size_t hash_count, std::vector<std::uint64_t> words);

    void add(std::string_view key);
    [[nodiscard]] bool may_contain(std::string_view key) const;
    [[nodiscard]] std::size_t bit_count() const noexcept { return bit_count_; }
    [[nodiscard]] std::size_t hash_count() const noexcept { return hash_count_; }
    [[nodiscard]] std::span<const std::uint64_t> words() const noexcept { return words_; }

private:
    static std::uint64_t hash(std::string_view key, std::uint64_t seed);

    std::size_t bit_count_;
    std::size_t hash_count_;
    std::vector<std::uint64_t> words_;
};

}  // namespace chronicle
