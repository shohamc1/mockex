#pragma once

#include "common/types.hpp"
#include "common/flat_hash_map.hpp"
#include <cstdint>
#include <map>

struct RestingOrder {
    uint64_t client_order_id;
    uint32_t instrument_id;
    Side side;
    uint32_t price_ticks;
    uint32_t remaining_qty;
};

class ExchangeSim {
public:
    explicit ExchangeSim(size_t expected_orders = 65536)
        : by_id_(expected_orders * 10 / 7) {}
    ExecutionReports submit(const OutboundOrder& ord) {
        ExecutionReports reports;

        uint32_t remaining = ord.qty;

        reports.push_back({
            ord.client_order_id,
            ExecType::Ack,
            0,
            remaining,
            0,
            0
        });

        if (ord.action == OrderAction::New) {
            if (ord.side == Side::Bid) {
                auto it = asks_.begin();
                while (it != asks_.end() &&
                       ord.price_ticks >= it->first &&
                       remaining > 0) {
                    auto& resting = it->second;
                    uint32_t fill_qty = std::min(remaining, resting.remaining_qty);
                    reports.push_back({
                        ord.client_order_id,
                        ExecType::Fill,
                        fill_qty,
                        remaining - fill_qty,
                        it->first,
                        0
                    });
                    remaining -= fill_qty;
                    resting.remaining_qty -= fill_qty;
                    if (resting.remaining_qty == 0) {
                        by_id_.erase(resting.client_order_id);
                        it = asks_.erase(it);
                    } else {
                        ++it;
                    }
                }
            } else {
                auto it = bids_.rbegin();
                while (it != bids_.rend() &&
                       ord.price_ticks <= it->first &&
                       remaining > 0) {
                    auto& resting = it->second;
                    uint32_t fill_qty = std::min(remaining, resting.remaining_qty);
                    reports.push_back({
                        ord.client_order_id,
                        ExecType::Fill,
                        fill_qty,
                        remaining - fill_qty,
                        it->first,
                        0
                    });
                    remaining -= fill_qty;
                    resting.remaining_qty -= fill_qty;
                    if (resting.remaining_qty == 0) {
                        by_id_.erase(resting.client_order_id);
                        auto fwd = std::next(it).base();
                        bids_.erase(fwd);
                        it = bids_.rbegin();
                    } else {
                        ++it;
                    }
                }
            }

            if (remaining > 0) {
                RestingOrder ro{ord.client_order_id, ord.instrument_id,
                                ord.side, ord.price_ticks, remaining};
                if (ord.side == Side::Bid) {
                    bids_[ord.price_ticks] = ro;
                } else {
                    asks_[ord.price_ticks] = ro;
                }
                by_id_.insert(ord.client_order_id, ro);
            }
        } else if (ord.action == OrderAction::Cancel) {
            auto* resting = by_id_.find(ord.client_order_id);
            if (resting) {
                if (resting->side == Side::Bid) {
                    bids_.erase(resting->price_ticks);
                } else {
                    asks_.erase(resting->price_ticks);
                }
                reports.push_back({
                    ord.client_order_id,
                    ExecType::Cancelled,
                    0,
                    0,
                    0,
                    0
                });
                by_id_.erase(ord.client_order_id);
            }
        }

        return reports;
    }

    void clear() {
        bids_.clear();
        asks_.clear();
        by_id_.clear();
    }

    size_t resting_count() const { return by_id_.size(); }

private:
    std::map<uint32_t, RestingOrder> bids_;
    std::map<uint32_t, RestingOrder> asks_;
    FlatHashMap<uint64_t, RestingOrder> by_id_;
};
