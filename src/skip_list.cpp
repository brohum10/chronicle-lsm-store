#include "chronicle/skip_list.hpp"

#include <algorithm>
#include <utility>

namespace chronicle {

SkipList::SkipList(std::uint32_t seed)
    : head_(std::make_unique<Node>(Entry{}, kMaxHeight)), random_(seed) {}

SkipList::~SkipList() = default;

void SkipList::upsert(Entry entry) {
    std::vector<Node*> update(kMaxHeight, head_.get());
    auto* current = head_.get();
    for (std::size_t level = height_; level-- > 0;) {
        while (current->forward[level] && current->forward[level]->entry.key < entry.key) {
            current = current->forward[level];
        }
        update[level] = current;
    }

    current = current->forward[0];
    if (current && current->entry.key == entry.key) {
        if (entry.sequence >= current->entry.sequence) current->entry = std::move(entry);
        return;
    }

    const auto node_height = random_height();
    if (node_height > height_) {
        for (std::size_t level = height_; level < node_height; ++level) update[level] = head_.get();
        height_ = node_height;
    }

    auto node = std::make_unique<Node>(std::move(entry), node_height);
    auto* inserted = node.get();
    for (std::size_t level = 0; level < node_height; ++level) {
        inserted->forward[level] = update[level]->forward[level];
        update[level]->forward[level] = inserted;
    }
    nodes_.push_back(std::move(node));
    ++size_;
}

std::optional<Entry> SkipList::get(std::string_view key) const {
    auto* current = head_.get();
    for (std::size_t level = height_; level-- > 0;) {
        while (current->forward[level] && current->forward[level]->entry.key < key) {
            current = current->forward[level];
        }
    }
    current = current->forward[0];
    if (current && current->entry.key == key) return current->entry;
    return std::nullopt;
}

std::vector<Entry> SkipList::entries() const {
    std::vector<Entry> result;
    result.reserve(size_);
    auto* current = head_->forward[0];
    while (current) {
        result.push_back(current->entry);
        current = current->forward[0];
    }
    return result;
}

void SkipList::clear() {
    std::fill(head_->forward.begin(), head_->forward.end(), nullptr);
    nodes_.clear();
    height_ = 1;
    size_ = 0;
}

std::size_t SkipList::random_height() {
    std::size_t height = 1;
    while (height < kMaxHeight && promote_(random_)) ++height;
    return height;
}

}  // namespace chronicle
