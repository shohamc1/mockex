#pragma once

#include "common/types.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

struct RawMsg {
    MsgType type;
    const uint8_t* data;
    uint16_t length;
};

class ReplayReader {
public:
    ReplayReader() = default;
    ~ReplayReader();

    bool open(const char* path);
    void close();
    std::optional<RawMsg> next();
    uint64_t last_seq_no() const { return last_seq_no_; }
    uint64_t msg_count() const { return msg_count_; }
    uint64_t gap_count() const { return gap_count_; }

private:
    std::vector<uint8_t> buf_;
    size_t pos_ = 0;
    size_t size_ = 0;
    uint64_t last_seq_no_ = 0;
    uint64_t msg_count_ = 0;
    uint64_t gap_count_ = 0;
    bool has_prev_seq_ = false;
};
