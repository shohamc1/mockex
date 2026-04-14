#pragma once

#include "common/types.hpp"
#include "common/flat_hash_map.hpp"
#include <cstdint>

enum class ItchParseResult : uint8_t {
    None = 0,
    Add,
    Modify,
    Cancel,
    Trade,
    Clear,
    Skipped
};

struct ItchParsedMsg {
    union {
        AddOrderMsg add;
        ModifyOrderMsg modify;
        CancelOrderMsg cancel;
        TradeMsg trade;
        ClearMsg clear;
    };
};

class ItchParser {
public:
    explicit ItchParser(size_t order_capacity = 1 << 20, uint16_t filter_locate = 0);

    ItchParseResult parse(char msg_type, const uint8_t* data, uint16_t len, ItchParsedMsg& out);

    struct Stats {
        uint64_t adds = 0;
        uint64_t cancels = 0;
        uint64_t trades = 0;
        uint64_t modifies = 0;
        uint64_t skipped = 0;
        uint64_t lookup_misses = 0;
    };

    const Stats& stats() const { return stats_; }

private:
    FlatHashMap<uint64_t, OrderState> orders_;
    uint64_t seq_no_ = 0;
    uint16_t filter_locate_;
    bool filtering_;
    Stats stats_;

    static uint16_t read_be16(const uint8_t* p);
    static uint32_t read_be32(const uint8_t* p);
    static uint64_t read_be64(const uint8_t* p);
    static uint64_t read_ts6(const uint8_t* p);
    static int32_t read_be32_signed(const uint8_t* p);

    bool check_locate(const uint8_t* data, uint16_t len) const;
};
