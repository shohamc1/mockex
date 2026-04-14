#pragma once

#include "common/types.hpp"
#include "common/flat_hash_map.hpp"
#include "exchange/exchange_sim.hpp"
#include <cstdint>

struct GatewayStats {
    uint64_t orders_sent = 0;
    uint64_t orders_cancelled = 0;
    uint64_t fills_received = 0;
    uint64_t rejects_received = 0;
};

struct FillInfo {
    uint32_t instrument_id;
    Side side;
    uint32_t fill_qty;
    uint32_t fill_price;
};

struct FillResults {
    static constexpr size_t kMaxFills = 4;
    FillInfo fills[kMaxFills];
    size_t count = 0;

    void push_back(const FillInfo& f) {
        if (count < kMaxFills) {
            fills[count++] = f;
        }
    }

    FillInfo* begin() { return fills; }
    FillInfo* end() { return fills + count; }
    const FillInfo* begin() const { return fills; }
    const FillInfo* end() const { return fills + count; }
    size_t size() const { return count; }
    bool empty() const { return count == 0; }
};

class OrderGateway {
public:
    explicit OrderGateway(ExchangeSim& sim, size_t expected_orders = 65536)
        : sim_(sim), active_orders_(expected_orders * 10 / 7) {}

    FillResults submit_intent(const OrderIntent& intent) {
        FillResults fills;

        if (intent.action == OrderAction::New) {
            OutboundOrder out;
            out.client_order_id = next_order_id_++;
            out.instrument_id = intent.instrument_id;
            out.side = intent.side;
            out.price_ticks = intent.price_ticks;
            out.qty = intent.qty;
            out.action = OrderAction::New;

            active_orders_.insert(out.client_order_id, {
                out.client_order_id,
                out.instrument_id,
                out.side,
                out.price_ticks,
                out.qty
            });

            stats_.orders_sent++;
            auto reports = sim_.submit(out);
            process_reports(reports, fills);
        } else if (intent.action == OrderAction::Cancel) {
            OutboundOrder out;
            out.client_order_id = intent.orig_client_order_id;
            out.instrument_id = intent.instrument_id;
            out.side = intent.side;
            out.price_ticks = intent.price_ticks;
            out.qty = intent.qty;
            out.action = OrderAction::Cancel;

            stats_.orders_cancelled++;
            auto reports = sim_.submit(out);
            process_reports(reports, fills);
        }

        return fills;
    }

    const GatewayStats& stats() const { return stats_; }
    uint64_t next_order_id() const { return next_order_id_; }

private:
    void process_reports(const ExecutionReports& reports, FillResults& fills) {
        for (size_t i = 0; i < reports.size(); ++i) {
            const auto& r = reports[i];
            switch (r.type) {
            case ExecType::Fill:
            case ExecType::PartialFill: {
                auto* order = active_orders_.find(r.client_order_id);
                if (order) {
                    fills.push_back({
                        order->instrument_id,
                        order->side,
                        r.filled_qty,
                        r.fill_price_ticks
                    });
                    stats_.fills_received++;
                    if (r.remaining_qty == 0) {
                        active_orders_.erase(r.client_order_id);
                    }
                }
                break;
            }
            case ExecType::Cancelled: {
                active_orders_.erase(r.client_order_id);
                break;
            }
            case ExecType::Reject:
                stats_.rejects_received++;
                active_orders_.erase(r.client_order_id);
                break;
            case ExecType::Ack:
                break;
            }
        }
    }

    ExchangeSim& sim_;
    uint64_t next_order_id_ = 1;
    FlatHashMap<uint64_t, RestingOrder> active_orders_;
    GatewayStats stats_;
};
