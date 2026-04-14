#pragma once

#include "common/types.hpp"
#include <cstdint>

enum class RiskResult : uint8_t {
    Accept,
    RejectQty,
    RejectPosition,
    RejectNotional,
    RejectPriceCollar
};

struct RiskStats {
    uint64_t total_checks = 0;
    uint64_t rejects = 0;
};

class RiskEngine {
public:
    explicit RiskEngine(RiskLimits limits) : limits_(limits) {}

    RiskResult validate(const OrderIntent& order,
                        const PortfolioState& portfolio,
                        uint32_t reference_price) const {
        stats_.total_checks++;

        if (order.qty > limits_.max_order_qty) {
            stats_.rejects++;
            return RiskResult::RejectQty;
        }

        int32_t new_pos = portfolio.position;
        if (order.side == Side::Bid) {
            new_pos += static_cast<int32_t>(order.qty);
        } else {
            new_pos -= static_cast<int32_t>(order.qty);
        }
        if (new_pos > limits_.max_position ||
            new_pos < -limits_.max_position) {
            stats_.rejects++;
            return RiskResult::RejectPosition;
        }

        uint64_t notional = static_cast<uint64_t>(order.qty) *
                            static_cast<uint64_t>(order.price_ticks);
        if (portfolio.notional_exposure + notional > limits_.max_notional) {
            stats_.rejects++;
            return RiskResult::RejectNotional;
        }

        if (reference_price != INVALID_PRICE && limits_.price_collar_ticks > 0) {
            int64_t diff = static_cast<int64_t>(order.price_ticks) -
                           static_cast<int64_t>(reference_price);
            if (diff < 0) diff = -diff;
            if (static_cast<uint64_t>(diff) > limits_.price_collar_ticks) {
                stats_.rejects++;
                return RiskResult::RejectPriceCollar;
            }
        }

        return RiskResult::Accept;
    }

    const RiskStats& stats() const { return stats_; }

private:
    RiskLimits limits_;
    mutable RiskStats stats_;
};
