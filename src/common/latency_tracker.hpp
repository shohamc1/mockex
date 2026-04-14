#pragma once

#include <cinttypes>
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <array>
#include <memory>

class LatencyTracker {
public:
    enum Label : size_t {
        MdIngress,
        BookUpdate,
        Strategy,
        Risk,
        Gateway,
        TickToAck,
        LabelCount
    };

    static constexpr const char* label_names[LabelCount] = {
        "md_ingress_ns", "book_update_ns", "strategy_ns",
        "risk_ns", "gateway_ns", "tick_to_ack_ns"
    };

    LatencyTracker() {
        for (size_t i = 0; i < LabelCount; ++i) {
            bufs_[i].samples = new uint64_t[kCap];
            bufs_[i].count = 0;
            bufs_[i].total = 0;
        }
    }

    ~LatencyTracker() {
        for (size_t i = 0; i < LabelCount; ++i) {
            delete[] bufs_[i].samples;
        }
    }

    LatencyTracker(const LatencyTracker&) = delete;
    LatencyTracker& operator=(const LatencyTracker&) = delete;

    LatencyTracker(LatencyTracker&& o) noexcept : tsc_freq_(o.tsc_freq_) {
        for (size_t i = 0; i < LabelCount; ++i) {
            bufs_[i] = o.bufs_[i];
            o.bufs_[i].samples = nullptr;
            o.bufs_[i].count = 0;
            o.bufs_[i].total = 0;
        }
    }

    LatencyTracker& operator=(LatencyTracker&& o) noexcept {
        if (this != &o) {
            for (size_t i = 0; i < LabelCount; ++i) {
                delete[] bufs_[i].samples;
                bufs_[i] = o.bufs_[i];
                o.bufs_[i].samples = nullptr;
                o.bufs_[i].count = 0;
                o.bufs_[i].total = 0;
            }
            tsc_freq_ = o.tsc_freq_;
        }
        return *this;
    }

    void record(Label label, uint64_t start_tsc, uint64_t end_tsc) {
        if (end_tsc <= start_tsc) return;
        record_raw(label, end_tsc - start_tsc);
    }

    void record_raw(Label label, uint64_t cycles) {
        if (cycles == 0) return;
        auto& b = bufs_[label];
        b.total++;
        if (b.count < kCap) {
            b.samples[b.count++] = cycles;
        }
    }

    void set_tsc_freq(uint64_t freq) { tsc_freq_ = freq; }
    uint64_t tsc_freq() const { return tsc_freq_; }

    void print_summary() const {
        printf("\n%-20s %10s %10s %10s %10s %10s %10s %10s\n",
               "stage", "count", "min", "p50", "p90", "p99", "p99.9", "max");
        printf("%-20s %10s %10s %10s %10s %10s %10s %10s\n",
               "", "", "(ns)", "(ns)", "(ns)", "(ns)", "(ns)", "(ns)");

        for (size_t i = 0; i < LabelCount; ++i) {
            const auto& b = bufs_[i];
            if (b.count == 0 && b.total == 0) continue;

            std::sort(b.samples, b.samples + b.count);

            uint64_t freq = tsc_freq_;
            auto ns = [freq](uint64_t c) -> uint64_t {
                return static_cast<uint64_t>(
                    static_cast<double>(c) * 1e9 / static_cast<double>(freq));
            };

            auto pct = [&](double p) -> uint64_t {
                size_t idx = static_cast<size_t>(p / 100.0 * (b.count - 1));
                return ns(b.samples[idx]);
            };

            printf("%-20s %10zu %10" PRIu64 " %10" PRIu64 " %10" PRIu64 " %10" PRIu64 " %10" PRIu64 " %10" PRIu64 "\n",
                   label_names[i],
                   b.total,
                   ns(b.samples[0]),
                   pct(50.0),
                   pct(90.0),
                   pct(99.0),
                   pct(99.9),
                   ns(b.samples[b.count - 1]));
        }
    }

private:
    static constexpr size_t kCap = 1 << 18;

    struct Buf {
        uint64_t* samples = nullptr;
        size_t count = 0;
        size_t total = 0;
    };

    Buf bufs_[LabelCount];
    uint64_t tsc_freq_ = 0;
};
