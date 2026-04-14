#include "feed/replay_reader.hpp"
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

ReplayReader::~ReplayReader() { close(); }

bool ReplayReader::open(const char* path) {
    int fd = ::open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "replay_reader: cannot open %s\n", path);
        return false;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        ::close(fd);
        return false;
    }
    size_ = static_cast<size_t>(st.st_size);

    if (size_ > 0) {
        data_ = static_cast<const uint8_t*>(mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd, 0));
        if (data_ == MAP_FAILED) {
            fprintf(stderr, "replay_reader: mmap failed\n");
            ::close(fd);
            data_ = nullptr;
            return false;
        }
    }
    ::close(fd);

    pos_ = 0;
    msg_count_ = 0;
    last_seq_no_ = 0;
    gap_count_ = 0;
    has_prev_seq_ = false;
    return true;
}

void ReplayReader::close() {
    if (data_ && size_ > 0) {
        munmap(const_cast<uint8_t*>(data_), size_);
    }
    data_ = nullptr;
    size_ = 0;
    pos_ = 0;
}

std::optional<RawMsg> ReplayReader::next() {
    while (pos_ + kWireHeaderSize <= size_) {
        const uint8_t* hdr = data_ + pos_;
        MsgType type = static_cast<MsgType>(hdr[0]);
        uint16_t payload_len;
        memcpy(&payload_len, hdr + 1, sizeof(payload_len));
        size_t total = kWireHeaderSize + payload_len;

        if (pos_ + total > size_) {
            fprintf(stderr, "replay_reader: truncated message at offset %zu\n", pos_);
            return std::nullopt;
        }

        const uint8_t* payload = data_ + pos_ + kWireHeaderSize;
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
