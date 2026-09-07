#include "chronicle/sstable.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <fstream>
#include <limits>
#include <span>
#include <stdexcept>
#include <system_error>
#include <unistd.h>

#include "chronicle/crc32.hpp"
#include "codec.hpp"

namespace chronicle {
namespace {

constexpr std::uint32_t kMagic = 0x54534843U;  // "CHST" in little-endian order.
constexpr std::uint32_t kVersion = 1;
constexpr std::uint32_t kMaximumRecordBytes = 8U * 1024U * 1024U;
constexpr std::uint64_t kMaximumBloomWords = 64U * 1024U * 1024U;

void write_bytes(std::ofstream& output, std::span<const std::byte> bytes) {
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!output) throw std::runtime_error("unable to write SSTable");
}

template <typename Integer>
void write_integer(std::ofstream& output, Integer value) {
    std::vector<std::byte> bytes;
    bytes.reserve(sizeof(Integer));
    codec::append_integer<Integer>(bytes, value);
    write_bytes(output, bytes);
}

template <typename Integer>
Integer read_integer(std::ifstream& input) {
    std::array<std::byte, sizeof(Integer)> bytes{};
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input) throw std::runtime_error("truncated SSTable header or record");
    std::size_t offset = 0;
    return codec::read_integer<Integer>(bytes, offset);
}

std::vector<std::byte> read_bytes(std::ifstream& input, std::size_t size) {
    std::vector<std::byte> bytes(size);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    if (!input && size != 0) throw std::runtime_error("truncated SSTable record");
    return bytes;
}

void sync_file(const std::filesystem::path& path) {
    const int descriptor = ::open(path.c_str(), O_RDONLY);
    if (descriptor < 0) throw std::system_error(errno, std::generic_category(), "unable to open SSTable for sync");
    const int result = ::fsync(descriptor);
    const int sync_error = errno;
    ::close(descriptor);
    if (result != 0) throw std::system_error(sync_error, std::generic_category(), "unable to sync SSTable");
}

void sync_directory(const std::filesystem::path& directory) {
    const int descriptor = ::open(directory.c_str(), O_RDONLY);
    if (descriptor < 0) return;
    ::fsync(descriptor);
    ::close(descriptor);
}

}  // namespace

SSTable::SSTable(
    std::filesystem::path path,
    BloomFilter filter,
    std::vector<IndexEntry> index,
    std::uint64_t max_sequence)
    : path_(std::move(path)),
      filter_(std::move(filter)),
      index_(std::move(index)),
      max_sequence_(max_sequence) {}

std::shared_ptr<SSTable> SSTable::create(
    const std::filesystem::path& final_path,
    std::vector<Entry> sorted_entries) {
    if (!std::is_sorted(sorted_entries.begin(), sorted_entries.end(), [](const Entry& left, const Entry& right) {
            return left.key < right.key;
        })) {
        throw std::invalid_argument("SSTable entries must be sorted by key");
    }
    for (std::size_t index = 1; index < sorted_entries.size(); ++index) {
        if (sorted_entries[index - 1].key == sorted_entries[index].key) {
            throw std::invalid_argument("SSTable cannot contain duplicate keys");
        }
    }

    std::filesystem::create_directories(final_path.parent_path());
    const auto temporary_path = final_path.string() + ".tmp-" + std::to_string(::getpid());
    BloomFilter filter(sorted_entries.size());
    std::uint64_t max_sequence = 0;
    for (const auto& entry : sorted_entries) {
        filter.add(entry.key);
        max_sequence = std::max(max_sequence, entry.sequence);
    }

    try {
        std::ofstream output(temporary_path, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("unable to create temporary SSTable");
        write_integer<std::uint32_t>(output, kMagic);
        write_integer<std::uint32_t>(output, kVersion);
        write_integer<std::uint64_t>(output, sorted_entries.size());
        write_integer<std::uint64_t>(output, max_sequence);
        write_integer<std::uint64_t>(output, filter.bit_count());
        write_integer<std::uint32_t>(output, static_cast<std::uint32_t>(filter.hash_count()));
        write_integer<std::uint64_t>(output, filter.words().size());
        for (const auto word : filter.words()) write_integer<std::uint64_t>(output, word);

        for (const auto& entry : sorted_entries) {
            const auto payload = codec::encode_entry(entry);
            write_integer<std::uint32_t>(output, static_cast<std::uint32_t>(payload.size()));
            write_bytes(output, payload);
            write_integer<std::uint32_t>(output, crc32(payload));
        }
        output.flush();
        if (!output) throw std::runtime_error("unable to flush temporary SSTable");
        output.close();
        sync_file(temporary_path);
        std::filesystem::rename(temporary_path, final_path);
        sync_directory(final_path.parent_path());
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary_path, ignored);
        throw;
    }
    return open(final_path);
}

std::shared_ptr<SSTable> SSTable::open(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("unable to open SSTable: " + path.string());
    if (read_integer<std::uint32_t>(input) != kMagic) throw std::runtime_error("invalid SSTable magic");
    if (read_integer<std::uint32_t>(input) != kVersion) throw std::runtime_error("unsupported SSTable version");
    const auto entry_count = read_integer<std::uint64_t>(input);
    const auto max_sequence = read_integer<std::uint64_t>(input);
    const auto bit_count = read_integer<std::uint64_t>(input);
    const auto hash_count = read_integer<std::uint32_t>(input);
    const auto word_count = read_integer<std::uint64_t>(input);
    if (word_count == 0 || word_count > kMaximumBloomWords || word_count * 64U != bit_count) {
        throw std::runtime_error("invalid SSTable Bloom filter metadata");
    }
    std::vector<std::uint64_t> words;
    words.reserve(static_cast<std::size_t>(word_count));
    for (std::uint64_t index = 0; index < word_count; ++index) {
        words.push_back(read_integer<std::uint64_t>(input));
    }
    BloomFilter filter(static_cast<std::size_t>(bit_count), hash_count, std::move(words));

    std::vector<IndexEntry> index;
    if (entry_count > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::runtime_error("SSTable has too many entries for this platform");
    }
    index.reserve(static_cast<std::size_t>(entry_count));
    std::uint64_t observed_max_sequence = 0;
    for (std::uint64_t item = 0; item < entry_count; ++item) {
        const auto record_offset = input.tellg();
        if (record_offset < 0) throw std::runtime_error("unable to locate SSTable record");
        const auto payload_size = read_integer<std::uint32_t>(input);
        if (payload_size > kMaximumRecordBytes) throw std::runtime_error("SSTable record is too large");
        const auto payload = read_bytes(input, payload_size);
        const auto stored_checksum = read_integer<std::uint32_t>(input);
        if (crc32(payload) != stored_checksum) throw std::runtime_error("SSTable checksum mismatch");
        const auto entry = codec::decode_entry(payload);
        if (!index.empty() && index.back().key >= entry.key) {
            throw std::runtime_error("SSTable keys are not strictly ordered");
        }
        observed_max_sequence = std::max(observed_max_sequence, entry.sequence);
        index.push_back({entry.key, static_cast<std::uint64_t>(record_offset)});
    }
    if (input.peek() != std::ifstream::traits_type::eof()) {
        throw std::runtime_error("unexpected trailing bytes in SSTable");
    }
    if (observed_max_sequence != max_sequence) throw std::runtime_error("invalid SSTable sequence metadata");
    return std::shared_ptr<SSTable>(new SSTable(path, std::move(filter), std::move(index), max_sequence));
}

std::optional<Entry> SSTable::get(std::string_view key) const {
    if (!filter_.may_contain(key)) return std::nullopt;
    const auto match = std::lower_bound(index_.begin(), index_.end(), key, [](const IndexEntry& item, std::string_view wanted) {
        return item.key < wanted;
    });
    if (match == index_.end() || match->key != key) return std::nullopt;
    return read_entry(match->offset);
}

std::vector<Entry> SSTable::entries() const {
    std::vector<Entry> result;
    result.reserve(index_.size());
    for (const auto& item : index_) result.push_back(read_entry(item.offset));
    return result;
}

Entry SSTable::read_entry(std::uint64_t offset) const {
    std::ifstream input(path_, std::ios::binary);
    if (!input) throw std::runtime_error("unable to open SSTable record");
    input.seekg(static_cast<std::streamoff>(offset));
    if (!input) throw std::runtime_error("unable to seek to SSTable record");
    const auto payload_size = read_integer<std::uint32_t>(input);
    if (payload_size > kMaximumRecordBytes) throw std::runtime_error("SSTable record is too large");
    const auto payload = read_bytes(input, payload_size);
    const auto stored_checksum = read_integer<std::uint32_t>(input);
    if (crc32(payload) != stored_checksum) throw std::runtime_error("SSTable checksum mismatch");
    return codec::decode_entry(payload);
}

}  // namespace chronicle
