#include "feed/itch_replay_reader.hpp"
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

ItchReplayReader::~ItchReplayReader() { close(); }

bool ItchReplayReader::open(const char* path) {
    int fd = ::open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "itch_replay_reader: cannot open %s\n", path);
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
            fprintf(stderr, "itch_replay_reader: mmap failed\n");
            ::close(fd);
            data_ = nullptr;
            return false;
        }
    }
    ::close(fd);

    pos_ = 0;
    msg_count_ = 0;
    return true;
}

void ItchReplayReader::close() {
    if (data_ && size_ > 0) {
        munmap(const_cast<uint8_t*>(data_), size_);
    }
    data_ = nullptr;
    size_ = 0;
    pos_ = 0;
}

std::optional<ItchRawMsg> ItchReplayReader::next() {
    if (pos_ + 2 > size_) return std::nullopt;

    const uint8_t* hdr = data_ + pos_;
    uint16_t payload_len = (static_cast<uint16_t>(hdr[0]) << 8) | hdr[1];

    if (pos_ + 2 + payload_len > size_) {
        fprintf(stderr, "itch_replay_reader: truncated message at offset %zu\n", pos_);
        return std::nullopt;
    }

    const uint8_t* payload = data_ + pos_ + 2;
    pos_ += 2 + payload_len;
    msg_count_++;

    if (payload_len < 1) return std::nullopt;

    return ItchRawMsg{static_cast<char>(payload[0]), payload, payload_len};
}
