#pragma once

#include <filesystem>
#include <vector>

#include "chronicle/entry.hpp"

namespace chronicle {

class WriteAheadLog {
public:
    explicit WriteAheadLog(std::filesystem::path path, bool sync_on_append = true);
    ~WriteAheadLog();
    WriteAheadLog(const WriteAheadLog&) = delete;
    WriteAheadLog& operator=(const WriteAheadLog&) = delete;

    void append(const Entry& entry);
    [[nodiscard]] std::vector<Entry> replay() const;
    void reset();

private:
    void open_append();

    std::filesystem::path path_;
    bool sync_on_append_;
    int descriptor_{-1};
};

}  // namespace chronicle
