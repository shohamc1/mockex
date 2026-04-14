#pragma once

#include <chrono>
#include <cstdint>

inline uint64_t rdtsc() {
#if defined(__x86_64__) || defined(_M_X64)
    unsigned int lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
#elif defined(__aarch64__)
    uint64_t val;
    __asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(val));
    return val;
#else
    return static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
#endif
}

inline uint64_t tsc_frequency() {
    static uint64_t freq = 0;
    if (freq != 0) return freq;
#if defined(__aarch64__)
    __asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(freq));
    return freq;
#else
    auto t0 = std::chrono::steady_clock::now();
    uint64_t c0 = rdtsc();
    volatile int sink = 0;
    for (int i = 0; i < 10000000; ++i) sink += i;
    (void)sink;
    auto t1 = std::chrono::steady_clock::now();
    uint64_t c1 = rdtsc();
    double elapsed_ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
    freq = static_cast<uint64_t>((static_cast<double>(c1 - c0) / elapsed_ns) * 1e9);
    return freq;
#endif
}

inline double tsc_to_ns(uint64_t cycles) {
    return static_cast<double>(cycles) * 1e9 / static_cast<double>(tsc_frequency());
}

inline uint64_t wall_now_ns() {
    return static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
}

inline uint64_t wall_now_us() {
    return wall_now_ns() / 1000;
}
