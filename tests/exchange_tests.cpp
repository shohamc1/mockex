#include "common/test_runner.hpp"
#include "exchange/exchange_sim.hpp"

TEST(exchange_ack_new_order) {
    ExchangeSim sim;
    OutboundOrder order{1, 0, Side::Bid, 10000, 100, OrderAction::New};
    auto reports = sim.submit(order);
    ASSERT_TRUE(!reports.empty());
    ASSERT_EQ(static_cast<int>(reports[0].type), static_cast<int>(ExecType::Ack));
}

TEST(exchange_immediate_fill_buy_cross) {
    ExchangeSim sim;
    OutboundOrder ask{1, 0, Side::Ask, 10000, 100, OrderAction::New};
    sim.submit(ask);

    OutboundOrder bid{2, 0, Side::Bid, 10000, 50, OrderAction::New};
    auto reports = sim.submit(bid);

    bool found_fill = false;
    for (auto& r : reports) {
        if (r.type == ExecType::Fill) {
            found_fill = true;
            ASSERT_EQ(r.filled_qty, 50u);
            ASSERT_EQ(r.fill_price_ticks, 10000u);
        }
    }
    ASSERT_TRUE(found_fill);
}

TEST(exchange_immediate_fill_sell_cross) {
    ExchangeSim sim;
    OutboundOrder bid{1, 0, Side::Bid, 10000, 100, OrderAction::New};
    sim.submit(bid);

    OutboundOrder ask{2, 0, Side::Ask, 10000, 50, OrderAction::New};
    auto reports = sim.submit(ask);

    bool found_fill = false;
    for (auto& r : reports) {
        if (r.type == ExecType::Fill) {
            found_fill = true;
            ASSERT_EQ(r.filled_qty, 50u);
        }
    }
    ASSERT_TRUE(found_fill);
}

TEST(exchange_partial_fill_leaves_resting) {
    ExchangeSim sim;
    OutboundOrder ask{1, 0, Side::Ask, 10000, 100, OrderAction::New};
    sim.submit(ask);

    OutboundOrder bid{2, 0, Side::Bid, 10000, 30, OrderAction::New};
    sim.submit(bid);

    ASSERT_EQ(sim.resting_count(), 1u);
}

TEST(exchange_resting_order_if_no_cross) {
    ExchangeSim sim;
    OutboundOrder bid{1, 0, Side::Bid, 9900, 100, OrderAction::New};
    auto reports = sim.submit(bid);
    ASSERT_EQ(sim.resting_count(), 1u);
    ASSERT_EQ(static_cast<int>(reports[0].type), static_cast<int>(ExecType::Ack));
}

TEST(exchange_cancel_order) {
    ExchangeSim sim;
    OutboundOrder order{1, 0, Side::Bid, 10000, 100, OrderAction::New};
    sim.submit(order);
    ASSERT_EQ(sim.resting_count(), 1u);

    OutboundOrder cancel{1, 0, Side::Bid, 10000, 100, OrderAction::Cancel};
    auto reports = sim.submit(cancel);

    bool found_cancel = false;
    for (auto& r : reports) {
        if (r.type == ExecType::Cancelled) found_cancel = true;
    }
    ASSERT_TRUE(found_cancel);
    ASSERT_EQ(sim.resting_count(), 0u);
}

TEST(exchange_aggressive_buy_through_multiple_levels) {
    ExchangeSim sim;
    sim.submit({1, 0, Side::Ask, 10000, 50, OrderAction::New});
    sim.submit({2, 0, Side::Ask, 10010, 50, OrderAction::New});

    auto reports = sim.submit({3, 0, Side::Bid, 10010, 80, OrderAction::New});

    int fills = 0;
    uint32_t total_filled = 0;
    for (auto& r : reports) {
        if (r.type == ExecType::Fill) {
            fills++;
            total_filled += r.filled_qty;
        }
    }
    ASSERT_EQ(fills, 2);
    ASSERT_EQ(total_filled, 80u);
}

int main() {
    printf("Running exchange tests...\n");
    return test::run_all();
}
