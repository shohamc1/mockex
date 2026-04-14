#pragma once

#include "common/types.hpp"
#include "common/flat_hash_map.hpp"
#include <cstdint>
#include <map>
#include <optional>

class OrderBook {
public:
    explicit OrderBook(uint32_t instrument_id)
        : instrument_id_(instrument_id), orders_(8 << 20) {}

    std::optional<BboUpdate> add_order(uint64_t order_id, uint32_t price,
                                        uint32_t qty, Side side) {
        if (orders_.find(order_id) != nullptr) return std::nullopt;

        uint32_t old_bb = cached_bb_;
        uint32_t old_bbq = cached_bbq_;
        uint32_t old_ba = cached_ba_;
        uint32_t old_baq = cached_baq_;

        orders_.insert(order_id, {price, qty, side});
        if (side == Side::Bid) {
            bids_[price] += qty;
            if (price > cached_bb_ || cached_bb_ == INVALID_PRICE) {
                cached_bb_ = price;
                cached_bbq_ = qty;
            } else if (price == cached_bb_) {
                cached_bbq_ += qty;
            }
        } else {
            asks_[price] += qty;
            if (price < cached_ba_ || cached_ba_ == INVALID_PRICE) {
                cached_ba_ = price;
                cached_baq_ = qty;
            } else if (price == cached_ba_) {
                cached_baq_ += qty;
            }
        }

        return maybe_bbo_update(old_bb, old_bbq, old_ba, old_baq);
    }

    std::optional<BboUpdate> modify_order(uint64_t order_id,
                                           uint32_t new_price,
                                           uint32_t new_qty) {
        auto* ord = orders_.find(order_id);
        if (ord == nullptr) return std::nullopt;

        uint32_t old_bb = cached_bb_;
        uint32_t old_bbq = cached_bbq_;
        uint32_t old_ba = cached_ba_;
        uint32_t old_baq = cached_baq_;

        if (ord->side == Side::Bid) {
            remove_from_side(bids_, ord->price_ticks, ord->qty);
            ord->price_ticks = new_price;
            ord->qty = new_qty;
            bids_[new_price] += new_qty;
        } else {
            remove_from_side(asks_, ord->price_ticks, ord->qty);
            ord->price_ticks = new_price;
            ord->qty = new_qty;
            asks_[new_price] += new_qty;
        }

        invalidate_cache();
        return maybe_bbo_update(old_bb, old_bbq, old_ba, old_baq);
    }

    std::optional<BboUpdate> cancel_order(uint64_t order_id) {
        auto* ord = orders_.find(order_id);
        if (ord == nullptr) return std::nullopt;

        uint32_t old_bb = cached_bb_;
        uint32_t old_bbq = cached_bbq_;
        uint32_t old_ba = cached_ba_;
        uint32_t old_baq = cached_baq_;

        if (ord->side == Side::Bid) {
            remove_from_side(bids_, ord->price_ticks, ord->qty);
        } else {
            remove_from_side(asks_, ord->price_ticks, ord->qty);
        }
        orders_.erase(order_id);

        invalidate_cache();
        return maybe_bbo_update(old_bb, old_bbq, old_ba, old_baq);
    }

    std::optional<BboUpdate> apply_trade(uint64_t order_id, uint32_t trade_qty) {
        auto* ord = orders_.find(order_id);
        if (ord == nullptr) return std::nullopt;

        uint32_t old_bb = cached_bb_;
        uint32_t old_bbq = cached_bbq_;
        uint32_t old_ba = cached_ba_;
        uint32_t old_baq = cached_baq_;

        if (ord->side == Side::Bid) {
            remove_from_side(bids_, ord->price_ticks, trade_qty);
        } else {
            remove_from_side(asks_, ord->price_ticks, trade_qty);
        }

        if (trade_qty >= ord->qty) {
            orders_.erase(order_id);
        } else {
            ord->qty -= trade_qty;
        }

        invalidate_cache();
        return maybe_bbo_update(old_bb, old_bbq, old_ba, old_baq);
    }

    void clear() {
        bids_.clear();
        asks_.clear();
        orders_.clear();
        cached_bb_ = INVALID_PRICE;
        cached_bbq_ = 0;
        cached_ba_ = INVALID_PRICE;
        cached_baq_ = 0;
    }

    uint32_t best_bid_price() const {
        return cached_bb_;
    }

    uint32_t best_ask_price() const {
        return cached_ba_;
    }

    uint32_t best_bid_qty() const {
        return cached_bbq_;
    }

    uint32_t best_ask_qty() const {
        return cached_baq_;
    }

    bool is_valid() const {
        return !bids_.empty() && !asks_.empty();
    }

    int64_t midprice() const {
        if (!is_valid()) return 0;
        return (static_cast<int64_t>(best_bid_price()) +
                static_cast<int64_t>(best_ask_price())) / 2;
    }

    uint32_t spread() const {
        if (!is_valid()) return 0;
        return best_ask_price() - best_bid_price();
    }

    uint32_t instrument_id() const { return instrument_id_; }

    size_t order_count() const { return orders_.size(); }

    size_t bid_levels() const { return bids_.size(); }

    size_t ask_levels() const { return asks_.size(); }

private:
    void remove_from_side(std::map<uint32_t, uint32_t>& side,
                           uint32_t price, uint32_t qty) {
        auto it = side.find(price);
        if (it == side.end()) return;
        if (it->second <= qty) {
            side.erase(it);
        } else {
            it->second -= qty;
        }
    }

    void invalidate_cache() {
        cached_bb_ = bids_.empty() ? INVALID_PRICE : bids_.rbegin()->first;
        cached_bbq_ = bids_.empty() ? 0 : bids_.rbegin()->second;
        cached_ba_ = asks_.empty() ? INVALID_PRICE : asks_.begin()->first;
        cached_baq_ = asks_.empty() ? 0 : asks_.begin()->second;
    }

    std::optional<BboUpdate> maybe_bbo_update(uint32_t old_bb, uint32_t old_bbq,
                                               uint32_t old_ba, uint32_t old_baq) const {
        if (cached_bb_ == old_bb && cached_ba_ == old_ba &&
            cached_bbq_ == old_bbq && cached_baq_ == old_baq) return std::nullopt;
        return BboUpdate{
            instrument_id_,
            cached_bb_,
            cached_bbq_,
            cached_ba_,
            cached_baq_,
            0
        };
    }

    uint32_t instrument_id_;
    std::map<uint32_t, uint32_t> bids_;
    std::map<uint32_t, uint32_t> asks_;
    FlatHashMap<uint64_t, OrderState> orders_;
    uint32_t cached_bb_ = INVALID_PRICE;
    uint32_t cached_bbq_ = 0;
    uint32_t cached_ba_ = INVALID_PRICE;
    uint32_t cached_baq_ = 0;
};
