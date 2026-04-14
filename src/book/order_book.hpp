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

        uint32_t old_best_bid = best_bid_price();
        uint32_t old_best_ask = best_ask_price();
        uint32_t old_bb_qty = best_bid_qty();
        uint32_t old_ba_qty = best_ask_qty();

        orders_.insert(order_id, {price, qty, side});
        if (side == Side::Bid) {
            bids_[price] += qty;
        } else {
            asks_[price] += qty;
        }

        return maybe_bbo_update(old_best_bid, old_bb_qty, old_best_ask, old_ba_qty);
    }

    std::optional<BboUpdate> modify_order(uint64_t order_id,
                                           uint32_t new_price,
                                           uint32_t new_qty) {
        auto* ord = orders_.find(order_id);
        if (ord == nullptr) return std::nullopt;

        uint32_t old_best_bid = best_bid_price();
        uint32_t old_best_ask = best_ask_price();
        uint32_t old_bb_qty = best_bid_qty();
        uint32_t old_ba_qty = best_ask_qty();

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

        return maybe_bbo_update(old_best_bid, old_bb_qty, old_best_ask, old_ba_qty);
    }

    std::optional<BboUpdate> cancel_order(uint64_t order_id) {
        auto* ord = orders_.find(order_id);
        if (ord == nullptr) return std::nullopt;

        uint32_t old_best_bid = best_bid_price();
        uint32_t old_best_ask = best_ask_price();
        uint32_t old_bb_qty = best_bid_qty();
        uint32_t old_ba_qty = best_ask_qty();

        if (ord->side == Side::Bid) {
            remove_from_side(bids_, ord->price_ticks, ord->qty);
        } else {
            remove_from_side(asks_, ord->price_ticks, ord->qty);
        }
        orders_.erase(order_id);

        return maybe_bbo_update(old_best_bid, old_bb_qty, old_best_ask, old_ba_qty);
    }

    std::optional<BboUpdate> apply_trade(uint64_t order_id, uint32_t trade_qty) {
        auto* ord = orders_.find(order_id);
        if (ord == nullptr) return std::nullopt;

        uint32_t old_best_bid = best_bid_price();
        uint32_t old_best_ask = best_ask_price();
        uint32_t old_bb_qty = best_bid_qty();
        uint32_t old_ba_qty = best_ask_qty();

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

        return maybe_bbo_update(old_best_bid, old_bb_qty, old_best_ask, old_ba_qty);
    }

    void clear() {
        bids_.clear();
        asks_.clear();
        orders_.clear();
    }

    uint32_t best_bid_price() const {
        if (bids_.empty()) return INVALID_PRICE;
        return bids_.rbegin()->first;
    }

    uint32_t best_ask_price() const {
        if (asks_.empty()) return INVALID_PRICE;
        return asks_.begin()->first;
    }

    uint32_t best_bid_qty() const {
        if (bids_.empty()) return 0;
        return bids_.rbegin()->second;
    }

    uint32_t best_ask_qty() const {
        if (asks_.empty()) return 0;
        return asks_.begin()->second;
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

    std::optional<BboUpdate> maybe_bbo_update(uint32_t old_bb, uint32_t old_bbq,
                                               uint32_t old_ba, uint32_t old_baq) const {
        uint32_t new_bb = best_bid_price();
        uint32_t new_ba = best_ask_price();
        uint32_t new_bbq = best_bid_qty();
        uint32_t new_baq = best_ask_qty();
        if (new_bb == old_bb && new_ba == old_ba &&
            new_bbq == old_bbq && new_baq == old_baq) return std::nullopt;
        return BboUpdate{
            instrument_id_,
            new_bb,
            new_bbq,
            new_ba,
            new_baq,
            0
        };
    }

    uint32_t instrument_id_;
    std::map<uint32_t, uint32_t> bids_;
    std::map<uint32_t, uint32_t> asks_;
    FlatHashMap<uint64_t, OrderState> orders_;
};
