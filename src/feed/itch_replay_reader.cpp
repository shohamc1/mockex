#include "feed/itch_replay_reader.hpp"
#include <cstdio>
#include <cstring>

ItchReplayReader::~ItchReplayReader() { close(); }

bool ItchReplayReader::open(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "itch_replay_reader: cannot open %s\n", path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    size_ = static_cast<size_t>(ftell(f));
    fseek(f, 0, SEEK_SET);

    buf_.resize(size_);
    if (fread(buf_.data(), 1, size_, f) != size_) {
        fprintf(stderr, "itch_replay_reader: read error\n");
        fclose(f);
        return false;
    }
    fclose(f);
    pos_ = 0;
    msg_count_ = 0;
    return true;
}

void ItchReplayReader::close() {
    buf_.clear();
    buf_.shrink_to_fit();
    pos_ = 0;
    size_ = 0;
}

std::optional<ItchRawMsg> ItchReplayReader::next() {
    if (pos_ + 2 > size_) return std::nullopt;

    const uint8_t* hdr = buf_.data() + pos_;
    uint16_t payload_len = (static_cast<uint16_t>(hdr[0]) << 8) | hdr[1];

    if (pos_ + 2 + payload_len > size_) {
        fprintf(stderr, "itch_replay_reader: truncated message at offset %zu\n", pos_);
        return std::nullopt;
    }

    const uint8_t* payload = buf_.data() + pos_ + 2;
    pos_ += 2 + payload_len;
    msg_count_++;

    if (payload_len < 1) return std::nullopt;

    return ItchRawMsg{static_cast<char>(payload[0]), payload, payload_len};
}
