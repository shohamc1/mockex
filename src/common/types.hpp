#pragma once

#include <cstddef>
#include <cstdint>

enum class MsgType : uint8_t {
    AddOrder = 0,
    ModifyOrder = 1,
    CancelOrder = 2,
    Trade = 3,
    Clear = 4
};

enum class Side : uint8_t {
    Bid = 0,
    Ask = 1
};

enum class OrderAction : uint8_t {
    New = 0,
    Cancel = 1,
    Modify = 2
};

enum class ExecType : uint8_t {
    Ack = 0,
    Reject = 1,
    Fill = 2,
    PartialFill = 3,
    Cancelled = 4
};

struct AddOrderMsg {
    uint64_t seq_no;
    uint64_t ts_exchange;
    uint64_t order_id;
    uint32_t instrument_id;
    uint32_t price_ticks;
    uint32_t qty;
    Side side;
};

struct ModifyOrderMsg {
    uint64_t seq_no;
    uint64_t ts_exchange;
    uint64_t order_id;
    uint32_t instrument_id;
    uint32_t new_price_ticks;
    uint32_t new_qty;
};

struct CancelOrderMsg {
    uint64_t seq_no;
    uint64_t ts_exchange;
    uint64_t order_id;
    uint32_t instrument_id;
    uint32_t canceled_qty;
};

struct TradeMsg {
    uint64_t seq_no;
    uint64_t ts_exchange;
    uint64_t order_id;
    uint32_t instrument_id;
    uint32_t price_ticks;
    uint32_t qty;
    Side side;
};

struct ClearMsg {
    uint64_t seq_no;
    uint64_t ts_exchange;
    uint32_t instrument_id;
};

inline constexpr size_t kWireHeaderSize = 3;
inline constexpr size_t kAddOrderWireSize = 37;
inline constexpr size_t kModifyOrderWireSize = 36;
inline constexpr size_t kCancelOrderWireSize = 32;
inline constexpr size_t kTradeWireSize = 37;
inline constexpr size_t kClearWireSize = 20;

struct BboUpdate {
    uint32_t instrument_id;
    uint32_t best_bid_px;
    uint32_t best_bid_qty;
    uint32_t best_ask_px;
    uint32_t best_ask_qty;
    uint64_t ts_local;
};

struct OrderIntent {
    uint32_t instrument_id;
    Side side;
    uint32_t price_ticks;
    uint32_t qty;
    OrderAction action;
    uint64_t orig_client_order_id;
};

struct OutboundOrder {
    uint64_t client_order_id;
    uint32_t instrument_id;
    Side side;
    uint32_t price_ticks;
    uint32_t qty;
    OrderAction action;
};

struct ExecutionReport {
    uint64_t client_order_id;
    ExecType type;
    uint32_t filled_qty;
    uint32_t remaining_qty;
    uint32_t fill_price_ticks;
    uint64_t ts_exchange_sim;
};

struct RiskLimits {
    int32_t max_position;
    uint32_t max_order_qty;
    uint64_t max_notional;
    uint32_t price_collar_ticks;
};

struct PortfolioState {
    int32_t position;
    int64_t realized_pnl;
    int64_t cash;
    uint64_t notional_exposure;
};

struct Timestamps {
    uint64_t ts_feed_arrival;
    uint64_t ts_post_parse;
    uint64_t ts_post_book;
    uint64_t ts_post_strategy;
    uint64_t ts_post_risk;
    uint64_t ts_post_gateway;
    uint64_t ts_fill_received;
};

struct OrderState {
    uint32_t price_ticks;
    uint32_t qty;
    Side side;
};

inline constexpr uint32_t INVALID_PRICE = UINT32_MAX;

struct StrategyOutput {
    static constexpr size_t kMaxOrders = 4;
    OrderIntent orders[kMaxOrders];
    size_t count = 0;

    void push_back(const OrderIntent& intent) {
        if (count < kMaxOrders) {
            orders[count++] = intent;
        }
    }

    OrderIntent* begin() { return orders; }
    OrderIntent* end() { return orders + count; }
    const OrderIntent* begin() const { return orders; }
    const OrderIntent* end() const { return orders + count; }
    size_t size() const { return count; }
    bool empty() const { return count == 0; }
};
