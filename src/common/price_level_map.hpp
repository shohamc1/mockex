#pragma once

#include "common/flat_hash_map.hpp"
#include <cstdint>

template <bool ForBids>
class PriceLevelMap {
    static constexpr uint32_t EMPTY_LEVEL = 0;

    static bool is_better(uint32_t a, uint32_t b) {
        if constexpr (ForBids) return a > b;
        else return a < b;
    }

public:
    PriceLevelMap() : levels_(128) {}

    void add(uint32_t price, uint32_t qty) {
        uint32_t* existing = levels_.find(price);
        if (existing) {
            *existing += qty;
            if (price == best_price_) best_qty_ = *existing;
            return;
        }
        levels_.insert(price, qty);
        if (best_price_ == UINT32_MAX || is_better(price, best_price_)) {
            best_price_ = price;
            best_qty_ = qty;
        }
    }

    bool remove(uint32_t price, uint32_t qty) {
        uint32_t* existing = levels_.find(price);
        if (!existing) return false;
        if (*existing <= qty) {
            levels_.erase(price);
            if (price == best_price_) rescan();
        } else {
            *existing -= qty;
            if (price == best_price_) best_qty_ = *existing;
        }
        return true;
    }

    uint32_t* find(uint32_t price) { return levels_.find(price); }

    uint32_t best_price() const { return best_price_; }
    uint32_t best_qty() const { return best_qty_; }
    bool empty() const { return levels_.empty(); }
    size_t size() const { return levels_.size(); }
    void clear() { levels_.clear(); best_price_ = UINT32_MAX; best_qty_ = 0; }
    void reserve(size_t) {}

private:
    void rescan() {
        best_price_ = UINT32_MAX;
        best_qty_ = 0;
        levels_.for_each([this](uint32_t price, uint32_t qty) {
            if (best_price_ == UINT32_MAX || is_better(price, best_price_)) {
                best_price_ = price;
                best_qty_ = qty;
            }
        });
    }

    FlatHashMap<uint32_t, uint32_t> levels_;
    uint32_t best_price_ = UINT32_MAX;
    uint32_t best_qty_ = 0;
};

using BidLevels = PriceLevelMap<true>;
using AskLevels = PriceLevelMap<false>;
