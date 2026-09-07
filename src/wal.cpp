#include "chronicle/wal.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <span>
#include <stdexcept>
#include <system_error>
#include <unistd.h>

#include "chronicle/crc32.hpp"
#include "codec.hpp"

namespace chronicle {
namespace {

constexpr std::uint32_t kMaximumRecordBytes = 8U * 1024U * 1024U;

void write_all(int descriptor, std::span<const std::byte> bytes) {
    std::size_t written = 0;
    while (written < bytes.size()) {
        const auto result = ::write(
            descriptor, bytes.data() + written, static_cast<std::size_t>(bytes.size() - written));
        if (result < 0) {
            if (errno == EINTR) continue;
            throw std::system_error(errno, std::generic_category(), "unable to append to WAL");
        }
        if (result == 0) throw std::runtime_error("WAL append made no progress");
        written += static_cast<std::size_t>(result);
    }
}

std::vector<std::byte> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("unable to open WAL for recovery: " + path.string());
    const auto end = input.tellg();
    if (end < 0) throw std::runtime_error("unable to determine WAL size");
    std::vector<std::byte> bytes(static_cast<std::size_t>(end));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input && !bytes.empty()) throw std::runtime_error("unable to read WAL");
    return bytes;
}

}  // namespace

WriteAheadLog::WriteAheadLog(std::filesystem::path path, bool sync_on_append)
    : path_(std::move(path)), sync_on_append_(sync_on_append) {
    std::filesystem::create_directories(path_.parent_path());
    open_append();
}

WriteAheadLog::~WriteAheadLog() {
    if (descriptor_ >= 0) ::close(descriptor_);
}

void WriteAheadLog::append(const Entry& entry) {
    const auto payload = codec::encode_entry(entry);
    std::vector<std::byte> record;
    record.reserve(sizeof(std::uint32_t) + payload.size() + sizeof(std::uint32_t));
    codec::append_integer<std::uint32_t>(record, static_cast<std::uint32_t>(payload.size()));
    record.insert(record.end(), payload.begin(), payload.end());
    codec::append_integer<std::uint32_t>(record, crc32(payload));
    write_all(descriptor_, record);
    if (sync_on_append_ && ::fsync(descriptor_) != 0) {
        throw std::system_error(errno, std::generic_category(), "unable to sync WAL");
    }
}

std::vector<Entry> WriteAheadLog::replay() const {
    const auto bytes = read_file(path_);
    std::vector<Entry> entries;
    std::size_t offset = 0;
    while (bytes.size() - offset >= sizeof(std::uint32_t)) {
        std::size_t cursor = offset;
        const auto payload_size = codec::read_integer<std::uint32_t>(bytes, cursor);
        if (payload_size > kMaximumRecordBytes) throw std::runtime_error("WAL record is too large");
        const auto total_size = static_cast<std::uint64_t>(payload_size) + sizeof(std::uint32_t);
        if (total_size > bytes.size() - cursor) break;

        const auto payload = std::span<const std::byte>(bytes).subspan(cursor, payload_size);
        cursor += payload_size;
        const auto stored_checksum = codec::read_integer<std::uint32_t>(bytes, cursor);
        if (crc32(payload) != stored_checksum) throw std::runtime_error("WAL checksum mismatch");
        entries.push_back(codec::decode_entry(payload));
        offset = cursor;
    }
    return entries;
}

void WriteAheadLog::reset() {
    if (descriptor_ >= 0) {
        if (::close(descriptor_) != 0) {
            descriptor_ = -1;
            throw std::system_error(errno, std::generic_category(), "unable to close WAL");
        }
        descriptor_ = -1;
    }
    descriptor_ = ::open(path_.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0644);
    if (descriptor_ < 0) {
        throw std::system_error(errno, std::generic_category(), "unable to reset WAL");
    }
    if (sync_on_append_ && ::fsync(descriptor_) != 0) {
        throw std::system_error(errno, std::generic_category(), "unable to sync reset WAL");
    }
}

void WriteAheadLog::open_append() {
    descriptor_ = ::open(path_.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (descriptor_ < 0) {
        throw std::system_error(errno, std::generic_category(), "unable to open WAL");
    }
}

}  // namespace chronicle
