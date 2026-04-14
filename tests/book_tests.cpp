#include "common/test_runner.hpp"
#include "book/order_book.hpp"

TEST(book_add_single_bid) {
    OrderBook book(0);
    auto bbo = book.add_order(1, 10000, 500, Side::Bid);
    ASSERT_TRUE(bbo.has_value());
    ASSERT_EQ(bbo->best_bid_px, 10000u);
    ASSERT_EQ(bbo->best_bid_qty, 500u);
    ASSERT_EQ(bbo->best_ask_px, INVALID_PRICE);
}

TEST(book_add_single_ask) {
    OrderBook book(0);
    auto bbo = book.add_order(1, 10100, 300, Side::Ask);
    ASSERT_TRUE(bbo.has_value());
    ASSERT_EQ(bbo->best_ask_px, 10100u);
    ASSERT_EQ(bbo->best_ask_qty, 300u);
    ASSERT_EQ(bbo->best_bid_px, INVALID_PRICE);
}

TEST(book_add_both_sides) {
    OrderBook book(0);
    book.add_order(1, 10000, 500, Side::Bid);
    book.add_order(2, 10100, 300, Side::Ask);
    ASSERT_TRUE(book.is_valid());
    ASSERT_EQ(book.best_bid_price(), 10000u);
    ASSERT_EQ(book.best_ask_price(), 10100u);
    ASSERT_EQ(book.spread(), 100u);
    ASSERT_EQ(book.midprice(), 10050);
}

TEST(book_best_bid_updates) {
    OrderBook book(0);
    book.add_order(1, 10000, 500, Side::Bid);
    auto bbo = book.add_order(2, 10050, 200, Side::Bid);
    ASSERT_TRUE(bbo.has_value());
    ASSERT_EQ(bbo->best_bid_px, 10050u);
    ASSERT_EQ(bbo->best_bid_qty, 200u);
}

TEST(book_cancel_updates_bbo) {
    OrderBook book(0);
    book.add_order(1, 10000, 500, Side::Bid);
    book.add_order(2, 10050, 200, Side::Bid);
    auto bbo = book.cancel_order(2);
    ASSERT_TRUE(bbo.has_value());
    ASSERT_EQ(bbo->best_bid_px, 10000u);
}

TEST(book_modify_price) {
    OrderBook book(0);
    book.add_order(1, 10000, 500, Side::Bid);
    auto bbo = book.modify_order(1, 10050, 500);
    ASSERT_TRUE(bbo.has_value());
    ASSERT_EQ(bbo->best_bid_px, 10050u);
}

TEST(book_trade_reduces_qty) {
    OrderBook book(0);
    book.add_order(1, 10000, 500, Side::Bid);
    auto bbo = book.apply_trade(1, 200);
    ASSERT_TRUE(bbo.has_value());
    ASSERT_EQ(bbo->best_bid_qty, 300u);
}

TEST(book_trade_removes_level) {
    OrderBook book(0);
    book.add_order(1, 10000, 500, Side::Bid);
    book.add_order(2, 10050, 200, Side::Bid);
    book.apply_trade(2, 200);
    ASSERT_EQ(book.best_bid_price(), 10000u);
}

TEST(book_duplicate_order_rejected) {
    OrderBook book(0);
    book.add_order(1, 10000, 500, Side::Bid);
    auto bbo = book.add_order(1, 10050, 200, Side::Bid);
    ASSERT_TRUE(!bbo.has_value());
}

TEST(book_cancel_nonexistent) {
    OrderBook book(0);
    auto bbo = book.cancel_order(999);
    ASSERT_TRUE(!bbo.has_value());
}

TEST(book_clear) {
    OrderBook book(0);
    book.add_order(1, 10000, 500, Side::Bid);
    book.add_order(2, 10100, 300, Side::Ask);
    book.clear();
    ASSERT_TRUE(!book.is_valid());
    ASSERT_EQ(book.order_count(), 0u);
}

TEST(book_multiple_levels_depth) {
    OrderBook book(0);
    book.add_order(1, 10000, 100, Side::Bid);
    book.add_order(2, 9990, 200, Side::Bid);
    book.add_order(3, 9980, 300, Side::Bid);
    ASSERT_EQ(book.bid_levels(), 3u);
    ASSERT_EQ(book.best_bid_price(), 10000u);
    ASSERT_EQ(book.best_bid_qty(), 100u);
}

TEST(book_aggregation_at_same_price) {
    OrderBook book(0);
    book.add_order(1, 10000, 100, Side::Bid);
    book.add_order(2, 10000, 200, Side::Bid);
    ASSERT_EQ(book.best_bid_qty(), 300u);
    ASSERT_EQ(book.bid_levels(), 1u);
}

TEST(book_no_bbo_change_returns_nullopt) {
    OrderBook book(0);
    book.add_order(1, 10000, 100, Side::Bid);
    auto bbo = book.add_order(2, 9990, 200, Side::Bid);
    ASSERT_TRUE(!bbo.has_value());
}

int main() {
    printf("Running book tests...\n");
    return test::run_all();
}
