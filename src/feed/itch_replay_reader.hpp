#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>

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
    const uint8_t* data_ = nullptr;
    size_t pos_ = 0;
    size_t size_ = 0;
    uint64_t msg_count_ = 0;
};
