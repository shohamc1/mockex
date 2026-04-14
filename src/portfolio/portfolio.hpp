#pragma once

#include "common/types.hpp"
#include <cstdlib>
#include <cstdint>

class Portfolio {
public:
    explicit Portfolio(uint32_t tick_value_cents = 1)
        : tick_value_cents_(tick_value_cents) {}

    void apply_fill(Side side, uint32_t qty, uint32_t price_ticks) {
        if (side == Side::Bid) {
            int64_t cost = static_cast<int64_t>(qty) * price_ticks * tick_value_cents_;
            cash_ -= cost;
            position_ += static_cast<int32_t>(qty);
        } else {
            int64_t proceeds = static_cast<int64_t>(qty) * price_ticks * tick_value_cents_;
            cash_ += proceeds;
            position_ -= static_cast<int32_t>(qty);
        }
    }

    int32_t position() const { return position_; }
    int64_t cash() const { return cash_; }
    int64_t realized_pnl() const { return realized_pnl_; }

    int64_t unrealized_pnl(uint32_t mid_price) const {
        return static_cast<int64_t>(position_) * mid_price * tick_value_cents_;
    }

    uint64_t notional_exposure(uint32_t price) const {
        return static_cast<uint64_t>(
            std::abs(static_cast<int64_t>(position_))) * price * tick_value_cents_;
    }

    PortfolioState snapshot(uint32_t price) const {
        return PortfolioState{
            position_,
            realized_pnl_,
            cash_,
            notional_exposure(price)
        };
    }

    void reset() {
        position_ = 0;
        cash_ = 0;
        realized_pnl_ = 0;
    }

private:
    int32_t position_ = 0;
    int64_t cash_ = 0;
    int64_t realized_pnl_ = 0;
    uint32_t tick_value_cents_;
};
