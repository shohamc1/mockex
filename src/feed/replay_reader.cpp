#include "feed/replay_reader.hpp"
#include <cstdio>
#include <cstring>

ReplayReader::~ReplayReader() { close(); }

bool ReplayReader::open(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "replay_reader: cannot open %s\n", path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    size_ = static_cast<size_t>(ftell(f));
    fseek(f, 0, SEEK_SET);

    buf_.resize(size_);
    if (fread(buf_.data(), 1, size_, f) != size_) {
        fprintf(stderr, "replay_reader: read error\n");
        fclose(f);
        return false;
    }
    fclose(f);
    pos_ = 0;
    msg_count_ = 0;
    last_seq_no_ = 0;
    gap_count_ = 0;
    has_prev_seq_ = false;
    return true;
}

void ReplayReader::close() {
    buf_.clear();
    buf_.shrink_to_fit();
    pos_ = 0;
    size_ = 0;
}

std::optional<RawMsg> ReplayReader::next() {
    while (pos_ + kWireHeaderSize <= size_) {
        const uint8_t* hdr = buf_.data() + pos_;
        MsgType type = static_cast<MsgType>(hdr[0]);
        uint16_t payload_len;
        memcpy(&payload_len, hdr + 1, sizeof(payload_len));
        size_t total = kWireHeaderSize + payload_len;

        if (pos_ + total > size_) {
            fprintf(stderr, "replay_reader: truncated message at offset %zu\n", pos_);
            return std::nullopt;
        }

        const uint8_t* payload = buf_.data() + pos_ + kWireHeaderSize;
        pos_ += total;
        msg_count_++;

        uint64_t seq = 0;
        if (payload_len >= sizeof(uint64_t)) {
            memcpy(&seq, payload, sizeof(uint64_t));
        }
        if (has_prev_seq_ && seq != last_seq_no_ + 1) {
            gap_count_++;
            fprintf(stderr, "replay_reader: seq gap %lu -> %lu\n",
                    (unsigned long)last_seq_no_, (unsigned long)seq);
        }
        last_seq_no_ = seq;
        has_prev_seq_ = true;

        return RawMsg{type, payload, payload_len};
    }
    return std::nullopt;
}
