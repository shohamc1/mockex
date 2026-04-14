#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

struct ItchRawMsg {
    char msg_type;
    const uint8_t* data;
    uint16_t length;
};

class ItchReplayReader {
public:
    ItchReplayReader() = default;
    ~ItchReplayReader();

    bool open(const char* path);
    void close();
    std::optional<ItchRawMsg> next();
    uint64_t msg_count() const { return msg_count_; }

private:
    std::vector<uint8_t> buf_;
    size_t pos_ = 0;
    size_t size_ = 0;
    uint64_t msg_count_ = 0;
};
