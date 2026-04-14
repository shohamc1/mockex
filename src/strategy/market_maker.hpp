#pragma once

#include "common/types.hpp"
#include <cstdint>

struct StrategyConfig {
    uint32_t base_spread_ticks = 2;
    uint32_t lot_size = 100;
    int32_t max_position = 1000;
    double risk_skew_factor = 0.5;
    uint32_t quote_offset_ticks = 1;
};

class MarketMaker {
public:
    explicit MarketMaker(StrategyConfig cfg = {})
        : cfg_(cfg) {}

    void on_fill(Side side, uint32_t qty) {
        if (side == Side::Bid) {
            inventory_ += static_cast<int32_t>(qty);
        } else {
            inventory_ -= static_cast<int32_t>(qty);
        }
    }

    StrategyOutput on_bbo(const BboUpdate& bbo) {
        StrategyOutput orders;

        if (bbo.best_bid_px == INVALID_PRICE ||
            bbo.best_ask_px == INVALID_PRICE) {
            return orders;
        }

        int64_t fair = (static_cast<int64_t>(bbo.best_bid_px) +
                        static_cast<int64_t>(bbo.best_ask_px)) / 2;

        int64_t skew = static_cast<int64_t>(
            static_cast<double>(inventory_) * cfg_.risk_skew_factor);

        int64_t bid_px = fair - cfg_.base_spread_ticks - skew;
        int64_t ask_px = fair + cfg_.base_spread_ticks - skew;

        if (bid_px <= 0 || ask_px <= 0) return orders;

        if (inventory_ < cfg_.max_position) {
            orders.push_back({
                bbo.instrument_id,
                Side::Bid,
                static_cast<uint32_t>(bid_px),
                cfg_.lot_size,
                OrderAction::New,
                0
            });
        }

        if (inventory_ > -cfg_.max_position) {
            orders.push_back({
                bbo.instrument_id,
                Side::Ask,
                static_cast<uint32_t>(ask_px),
                cfg_.lot_size,
                OrderAction::New,
                0
            });
        }

        return orders;
    }

    int32_t inventory() const { return inventory_; }

private:
    StrategyConfig cfg_;
    int32_t inventory_ = 0;
};
