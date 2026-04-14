#pragma once

#include "common/types.hpp"
#include "exchange/exchange_sim.hpp"
#include <cstdint>
#include <unordered_map>
#include <vector>

struct GatewayStats {
    uint64_t orders_sent = 0;
    uint64_t orders_cancelled = 0;
    uint64_t fills_received = 0;
    uint64_t rejects_received = 0;
};

class OrderGateway {
public:
    explicit OrderGateway(ExchangeSim& sim) : sim_(sim) {}

    struct FillInfo {
        uint32_t instrument_id;
        Side side;
        uint32_t fill_qty;
        uint32_t fill_price;
    };

    std::vector<FillInfo> submit_intent(const OrderIntent& intent) {
        std::vector<FillInfo> fills;

        if (intent.action == OrderAction::New) {
            OutboundOrder out;
            out.client_order_id = next_order_id_++;
            out.instrument_id = intent.instrument_id;
            out.side = intent.side;
            out.price_ticks = intent.price_ticks;
            out.qty = intent.qty;
            out.action = OrderAction::New;

            active_orders_[out.client_order_id] = {
                out.client_order_id,
                out.instrument_id,
                out.side,
                out.price_ticks,
                out.qty
            };

            stats_.orders_sent++;
            auto reports = sim_.submit(out);
            fills = process_reports(reports);
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
            fills = process_reports(reports);
        }

        return fills;
    }

    const GatewayStats& stats() const { return stats_; }
    uint64_t next_order_id() const { return next_order_id_; }

private:
    std::vector<FillInfo> process_reports(const std::vector<ExecutionReport>& reports) {
        std::vector<FillInfo> fills;
        for (auto& r : reports) {
            switch (r.type) {
            case ExecType::Fill:
            case ExecType::PartialFill: {
                auto it = active_orders_.find(r.client_order_id);
                if (it != active_orders_.end()) {
                    fills.push_back({
                        it->second.instrument_id,
                        it->second.side,
                        r.filled_qty,
                        r.fill_price_ticks
                    });
                    stats_.fills_received++;
                    if (r.remaining_qty == 0) {
                        active_orders_.erase(it);
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
        return fills;
    }

    ExchangeSim& sim_;
    uint64_t next_order_id_ = 1;
    std::unordered_map<uint64_t, RestingOrder> active_orders_;
    GatewayStats stats_;
};
