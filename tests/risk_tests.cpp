#include "common/test_runner.hpp"
#include "risk/risk_engine.hpp"

TEST(risk_accept_valid_order) {
    RiskLimits limits{1000, 500, UINT64_MAX, 100};
    RiskEngine engine(limits);
    OrderIntent order{0, Side::Bid, 10000, 100, OrderAction::New, 0};
    PortfolioState portfolio{0, 0, 0, 0};
    auto result = engine.validate(order, portfolio, 10000);
    ASSERT_EQ(static_cast<int>(result), static_cast<int>(RiskResult::Accept));
}

TEST(risk_reject_qty_too_large) {
    RiskLimits limits{1000, 50, UINT64_MAX, 100};
    RiskEngine engine(limits);
    OrderIntent order{0, Side::Bid, 10000, 100, OrderAction::New, 0};
    PortfolioState portfolio{0, 0, 0, 0};
    auto result = engine.validate(order, portfolio, 10000);
    ASSERT_EQ(static_cast<int>(result), static_cast<int>(RiskResult::RejectQty));
}

TEST(risk_reject_position_breach_long) {
    RiskLimits limits{100, 500, UINT64_MAX, 100};
    RiskEngine engine(limits);
    OrderIntent order{0, Side::Bid, 10000, 50, OrderAction::New, 0};
    PortfolioState portfolio{80, 0, 0, 0};
    auto result = engine.validate(order, portfolio, 10000);
    ASSERT_EQ(static_cast<int>(result), static_cast<int>(RiskResult::RejectPosition));
}

TEST(risk_reject_position_breach_short) {
    RiskLimits limits{100, 500, UINT64_MAX, 100};
    RiskEngine engine(limits);
    OrderIntent order{0, Side::Ask, 10000, 50, OrderAction::New, 0};
    PortfolioState portfolio{-80, 0, 0, 0};
    auto result = engine.validate(order, portfolio, 10000);
    ASSERT_EQ(static_cast<int>(result), static_cast<int>(RiskResult::RejectPosition));
}

TEST(risk_reject_price_collar) {
    RiskLimits limits{1000, 500, UINT64_MAX, 10};
    RiskEngine engine(limits);
    OrderIntent order{0, Side::Bid, 10500, 100, OrderAction::New, 0};
    PortfolioState portfolio{0, 0, 0, 0};
    auto result = engine.validate(order, portfolio, 10000);
    ASSERT_EQ(static_cast<int>(result), static_cast<int>(RiskResult::RejectPriceCollar));
}

TEST(risk_accept_within_collar) {
    RiskLimits limits{1000, 500, UINT64_MAX, 100};
    RiskEngine engine(limits);
    OrderIntent order{0, Side::Bid, 10050, 100, OrderAction::New, 0};
    PortfolioState portfolio{0, 0, 0, 0};
    auto result = engine.validate(order, portfolio, 10000);
    ASSERT_EQ(static_cast<int>(result), static_cast<int>(RiskResult::Accept));
}

TEST(risk_stats_tracking) {
    RiskLimits limits{100, 50, UINT64_MAX, 100};
    RiskEngine engine(limits);
    OrderIntent bad{0, Side::Bid, 10000, 100, OrderAction::New, 0};
    OrderIntent good{0, Side::Bid, 10000, 10, OrderAction::New, 0};
    PortfolioState portfolio{0, 0, 0, 0};
    engine.validate(bad, portfolio, 10000);
    engine.validate(good, portfolio, 10000);
    ASSERT_EQ(engine.stats().total_checks, 2u);
    ASSERT_EQ(engine.stats().rejects, 1u);
}

int main() {
    printf("Running risk tests...\n");
    return test::run_all();
}
