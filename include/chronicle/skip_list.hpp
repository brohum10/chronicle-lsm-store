#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <random>
#include <string_view>
#include <vector>

#include "chronicle/entry.hpp"

namespace chronicle {

class SkipList {
public:
    explicit SkipList(std::uint32_t seed = 0xC0FFEEU);
    ~SkipList();
    SkipList(const SkipList&) = delete;
    SkipList& operator=(const SkipList&) = delete;

    void upsert(Entry entry);
    [[nodiscard]] std::optional<Entry> get(std::string_view key) const;
    [[nodiscard]] std::vector<Entry> entries() const;
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    void clear();

private:
    static constexpr std::size_t kMaxHeight = 18;

    struct Node {
        explicit Node(Entry value, std::size_t height)
            : entry(std::move(value)), forward(height, nullptr) {}
        Entry entry;
        std::vector<Node*> forward;
    };

    [[nodiscard]] std::size_t random_height();

    std::unique_ptr<Node> head_;
    std::vector<std::unique_ptr<Node>> nodes_;
    std::mt19937 random_;
    std::bernoulli_distribution promote_{0.5};
    std::size_t height_{1};
    std::size_t size_{};
};

}  // namespace chronicle
