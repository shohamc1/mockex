#include "feed/itch_parser.hpp"

ItchParser::ItchParser(size_t order_capacity, uint16_t filter_locate)
    : orders_(order_capacity), filter_locate_(filter_locate),
      filtering_(filter_locate != 0) {}

uint16_t ItchParser::read_be16(const uint8_t* p) {
    return (static_cast<uint16_t>(p[0]) << 8) | p[1];
}

uint32_t ItchParser::read_be32(const uint8_t* p) {
    return (static_cast<uint32_t>(read_be16(p)) << 16) | read_be16(p + 2);
}

uint64_t ItchParser::read_be64(const uint8_t* p) {
    return (static_cast<uint64_t>(read_be32(p)) << 32) | read_be32(p + 4);
}

uint64_t ItchParser::read_ts6(const uint8_t* p) {
    return (static_cast<uint64_t>(p[0]) << 40) |
           (static_cast<uint64_t>(p[1]) << 32) |
           (static_cast<uint64_t>(p[2]) << 24) |
           (static_cast<uint64_t>(p[3]) << 16) |
           (static_cast<uint64_t>(p[4]) << 8) |
           static_cast<uint64_t>(p[5]);
}

int32_t ItchParser::read_be32_signed(const uint8_t* p) {
    return static_cast<int32_t>(read_be32(p));
}

bool ItchParser::check_locate(const uint8_t* data, uint16_t len) const {
    if (!filtering_) return true;
    if (len < 3) return false;
    uint16_t loc = read_be16(data + 1);
    return loc == filter_locate_ || loc == 0;
}

ItchParseResult ItchParser::parse(char msg_type, const uint8_t* data, uint16_t len, ItchParsedMsg& out) {
    switch (msg_type) {
    case 'A': {
        if (len < 36 || !check_locate(data, len)) { stats_.skipped++; return ItchParseResult::Skipped; }

        uint64_t ts = read_ts6(data + 5);
        uint64_t order_id = read_be64(data + 11);
        Side side = (data[19] == 'B') ? Side::Bid : Side::Ask;
        uint32_t qty = read_be32(data + 20);
        int32_t price_raw = read_be32_signed(data + 32);
        uint32_t price_ticks = static_cast<uint32_t>(price_raw / 100);

        if (qty == 0 || price_ticks == 0) { stats_.skipped++; return ItchParseResult::Skipped; }

        orders_.insert(order_id, {price_ticks, qty, side});
        seq_no_++;

        out.add.seq_no = seq_no_;
        out.add.ts_exchange = ts;
        out.add.order_id = order_id;
        out.add.instrument_id = 0;
        out.add.price_ticks = price_ticks;
        out.add.qty = qty;
        out.add.side = side;
        stats_.adds++;
        return ItchParseResult::Add;
    }
    case 'F': {
        if (len < 40 || !check_locate(data, len)) { stats_.skipped++; return ItchParseResult::Skipped; }

        uint64_t ts = read_ts6(data + 5);
        uint64_t order_id = read_be64(data + 11);
        Side side = (data[19] == 'B') ? Side::Bid : Side::Ask;
        uint32_t qty = read_be32(data + 20);
        int32_t price_raw = read_be32_signed(data + 32);
        uint32_t price_ticks = static_cast<uint32_t>(price_raw / 100);

        if (qty == 0 || price_ticks == 0) { stats_.skipped++; return ItchParseResult::Skipped; }

        orders_.insert(order_id, {price_ticks, qty, side});
        seq_no_++;

        out.add.seq_no = seq_no_;
        out.add.ts_exchange = ts;
        out.add.order_id = order_id;
        out.add.instrument_id = 0;
        out.add.price_ticks = price_ticks;
        out.add.qty = qty;
        out.add.side = side;
        stats_.adds++;
        return ItchParseResult::Add;
    }
    case 'E': {
        if (len < 31 || !check_locate(data, len)) { stats_.skipped++; return ItchParseResult::Skipped; }

        uint64_t ts = read_ts6(data + 5);
        uint64_t order_id = read_be64(data + 11);
        uint32_t exec_qty = read_be32(data + 19);

        auto* state = orders_.find(order_id);
        if (!state) { stats_.lookup_misses++; stats_.skipped++; return ItchParseResult::Skipped; }

        uint32_t px = state->price_ticks;
        Side side = state->side;
        uint32_t remaining = state->qty;

        seq_no_++;

        if (exec_qty >= remaining) {
            orders_.erase(order_id);
        } else {
            state->qty = remaining - exec_qty;
        }

        out.trade.seq_no = seq_no_;
        out.trade.ts_exchange = ts;
        out.trade.order_id = order_id;
        out.trade.instrument_id = 0;
        out.trade.price_ticks = px;
        out.trade.qty = exec_qty;
        out.trade.side = side;
        stats_.trades++;
        return ItchParseResult::Trade;
    }
    case 'C': {
        if (len < 36 || !check_locate(data, len)) { stats_.skipped++; return ItchParseResult::Skipped; }

        uint64_t ts = read_ts6(data + 5);
        uint64_t order_id = read_be64(data + 11);
        uint32_t exec_qty = read_be32(data + 19);
        int32_t price_raw = read_be32_signed(data + 32);
        uint32_t price_ticks = static_cast<uint32_t>(price_raw / 100);

        Side side = Side::Bid;
        auto* state = orders_.find(order_id);
        if (state) {
            side = state->side;
            uint32_t remaining = state->qty;
            if (exec_qty >= remaining) {
                orders_.erase(order_id);
            } else {
                state->qty = remaining - exec_qty;
            }
        } else {
            stats_.lookup_misses++;
        }

        seq_no_++;

        out.trade.seq_no = seq_no_;
        out.trade.ts_exchange = ts;
        out.trade.order_id = order_id;
        out.trade.instrument_id = 0;
        out.trade.price_ticks = price_ticks;
        out.trade.qty = exec_qty;
        out.trade.side = side;
        stats_.trades++;
        return ItchParseResult::Trade;
    }
    case 'X': {
        if (len < 24 || !check_locate(data, len)) { stats_.skipped++; return ItchParseResult::Skipped; }

        uint64_t ts = read_ts6(data + 5);
        uint64_t order_id = read_be64(data + 11);
        uint32_t cancel_qty = read_be32(data + 19);

        auto* state = orders_.find(order_id);
        if (!state) { stats_.lookup_misses++; stats_.skipped++; return ItchParseResult::Skipped; }

        uint32_t remaining = state->qty;

        seq_no_++;

        if (cancel_qty >= remaining) {
            orders_.erase(order_id);
        } else {
            state->qty = remaining - cancel_qty;
        }

        out.cancel.seq_no = seq_no_;
        out.cancel.ts_exchange = ts;
        out.cancel.order_id = order_id;
        out.cancel.instrument_id = 0;
        out.cancel.canceled_qty = cancel_qty;
        stats_.cancels++;
        return ItchParseResult::Cancel;
    }
    case 'D': {
        if (len < 19 || !check_locate(data, len)) { stats_.skipped++; return ItchParseResult::Skipped; }

        uint64_t ts = read_ts6(data + 5);
        uint64_t order_id = read_be64(data + 11);

        auto* state = orders_.find(order_id);
        if (!state) { stats_.lookup_misses++; stats_.skipped++; return ItchParseResult::Skipped; }

        uint32_t remaining = state->qty;

        seq_no_++;
        orders_.erase(order_id);

        out.cancel.seq_no = seq_no_;
        out.cancel.ts_exchange = ts;
        out.cancel.order_id = order_id;
        out.cancel.instrument_id = 0;
        out.cancel.canceled_qty = remaining;
        stats_.cancels++;
        return ItchParseResult::Cancel;
    }
    case 'U': {
        if (len < 35 || !check_locate(data, len)) { stats_.skipped++; return ItchParseResult::Skipped; }

        uint64_t ts = read_ts6(data + 5);
        uint64_t orig_order_id = read_be64(data + 11);
        uint64_t new_order_id = read_be64(data + 19);
        uint32_t new_qty = read_be32(data + 27);
        int32_t new_price_raw = read_be32_signed(data + 31);
        uint32_t new_price = static_cast<uint32_t>(new_price_raw / 100);

        auto* state = orders_.find(orig_order_id);
        if (!state) { stats_.lookup_misses++; stats_.skipped++; return ItchParseResult::Skipped; }

        Side side = state->side;
        orders_.erase(orig_order_id);
        orders_.insert(new_order_id, {new_price, new_qty, side});

        seq_no_++;

        out.modify.seq_no = seq_no_;
        out.modify.ts_exchange = ts;
        out.modify.order_id = orig_order_id;
        out.modify.instrument_id = 0;
        out.modify.new_price_ticks = new_price;
        out.modify.new_qty = new_qty;
        stats_.modifies++;
        return ItchParseResult::Modify;
    }
    case 'P': {
        if (len < 44 || !check_locate(data, len)) { stats_.skipped++; return ItchParseResult::Skipped; }

        uint64_t ts = read_ts6(data + 5);
        uint64_t order_id = read_be64(data + 11);
        Side side = (data[19] == 'B') ? Side::Bid : Side::Ask;
        uint32_t qty = read_be32(data + 20);
        int32_t price_raw = read_be32_signed(data + 32);
        uint32_t price_ticks = static_cast<uint32_t>(price_raw / 100);

        if (qty == 0 || price_ticks == 0) { stats_.skipped++; return ItchParseResult::Skipped; }

        seq_no_++;

        out.trade.seq_no = seq_no_;
        out.trade.ts_exchange = ts;
        out.trade.order_id = order_id;
        out.trade.instrument_id = 0;
        out.trade.price_ticks = price_ticks;
        out.trade.qty = qty;
        out.trade.side = side;
        stats_.trades++;
        return ItchParseResult::Trade;
    }
    default:
        stats_.skipped++;
        return ItchParseResult::Skipped;
    }
}
