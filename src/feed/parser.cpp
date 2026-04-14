#include "feed/parser.hpp"
#include <cstring>

namespace {

inline uint64_t read_u64(const uint8_t* p) {
    uint64_t v;
    memcpy(&v, p, sizeof(v));
    return v;
}

inline uint32_t read_u32(const uint8_t* p) {
    uint32_t v;
    memcpy(&v, p, sizeof(v));
    return v;
}

inline uint8_t read_u8(const uint8_t* p) {
    return *p;
}

}

ParsedMsg Parser::parse(MsgType type, const uint8_t* data, size_t len) {
    switch (type) {
    case MsgType::AddOrder: {
        if (len < kAddOrderWireSize)
            return ParseError{"add_order: truncated"};
        AddOrderMsg msg;
        msg.seq_no       = read_u64(data + 0);
        msg.ts_exchange  = read_u64(data + 8);
        msg.order_id     = read_u64(data + 16);
        msg.instrument_id = read_u32(data + 24);
        msg.price_ticks  = read_u32(data + 28);
        msg.qty          = read_u32(data + 32);
        msg.side         = static_cast<Side>(read_u8(data + 36));
        if (msg.qty == 0)
            return ParseError{"add_order: zero qty"};
        if (msg.price_ticks == 0)
            return ParseError{"add_order: zero price"};
        if (msg.side != Side::Bid && msg.side != Side::Ask)
            return ParseError{"add_order: invalid side"};
        return ParsedAdd{msg};
    }
    case MsgType::ModifyOrder: {
        if (len < kModifyOrderWireSize)
            return ParseError{"modify_order: truncated"};
        ModifyOrderMsg msg;
        msg.seq_no        = read_u64(data + 0);
        msg.ts_exchange   = read_u64(data + 8);
        msg.order_id      = read_u64(data + 16);
        msg.instrument_id = read_u32(data + 24);
        msg.new_price_ticks = read_u32(data + 28);
        msg.new_qty       = read_u32(data + 32);
        if (msg.new_qty == 0)
            return ParseError{"modify_order: zero qty"};
        return ParsedModify{msg};
    }
    case MsgType::CancelOrder: {
        if (len < kCancelOrderWireSize)
            return ParseError{"cancel_order: truncated"};
        CancelOrderMsg msg;
        msg.seq_no        = read_u64(data + 0);
        msg.ts_exchange   = read_u64(data + 8);
        msg.order_id      = read_u64(data + 16);
        msg.instrument_id = read_u32(data + 24);
        msg.canceled_qty  = read_u32(data + 28);
        return ParsedCancel{msg};
    }
    case MsgType::Trade: {
        if (len < kTradeWireSize)
            return ParseError{"trade: truncated"};
        TradeMsg msg;
        msg.seq_no        = read_u64(data + 0);
        msg.ts_exchange   = read_u64(data + 8);
        msg.order_id      = read_u64(data + 16);
        msg.instrument_id = read_u32(data + 24);
        msg.price_ticks   = read_u32(data + 28);
        msg.qty           = read_u32(data + 32);
        msg.side          = static_cast<Side>(read_u8(data + 36));
        if (msg.qty == 0)
            return ParseError{"trade: zero qty"};
        return ParsedTrade{msg};
    }
    case MsgType::Clear: {
        if (len < kClearWireSize)
            return ParseError{"clear: truncated"};
        ClearMsg msg;
        msg.seq_no        = read_u64(data + 0);
        msg.ts_exchange   = read_u64(data + 8);
        msg.instrument_id = read_u32(data + 16);
        return ParsedClear{msg};
    }
    default:
        return ParseError{"unknown message type"};
    }
}
