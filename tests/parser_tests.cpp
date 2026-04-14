#include "common/test_runner.hpp"
#include "feed/parser.hpp"
#include <cstring>

TEST(parse_add_order_valid) {
    AddOrderMsg msg{};
    msg.seq_no = 1;
    msg.ts_exchange = 1000;
    msg.order_id = 42;
    msg.instrument_id = 0;
    msg.price_ticks = 10000;
    msg.qty = 500;
    msg.side = Side::Bid;

    auto parsed = Parser::parse(MsgType::AddOrder,
        reinterpret_cast<const uint8_t*>(&msg), sizeof(msg));
    auto* add = std::get_if<ParsedAdd>(&parsed);
    ASSERT_TRUE(add != nullptr);
    ASSERT_EQ(add->msg.order_id, 42u);
    ASSERT_EQ(add->msg.price_ticks, 10000u);
    ASSERT_EQ(add->msg.qty, 500u);
    ASSERT_EQ(static_cast<int>(add->msg.side), static_cast<int>(Side::Bid));
}

TEST(parse_add_order_truncated) {
    AddOrderMsg msg{};
    msg.seq_no = 1;
    auto parsed = Parser::parse(MsgType::AddOrder,
        reinterpret_cast<const uint8_t*>(&msg), 10);
    auto* err = std::get_if<ParseError>(&parsed);
    ASSERT_TRUE(err != nullptr);
}

TEST(parse_add_order_zero_qty) {
    AddOrderMsg msg{};
    msg.seq_no = 1;
    msg.qty = 0;
    msg.price_ticks = 100;
    msg.side = Side::Bid;
    auto parsed = Parser::parse(MsgType::AddOrder,
        reinterpret_cast<const uint8_t*>(&msg), sizeof(msg));
    auto* err = std::get_if<ParseError>(&parsed);
    ASSERT_TRUE(err != nullptr);
}

TEST(parse_cancel_order_valid) {
    CancelOrderMsg msg{};
    msg.seq_no = 5;
    msg.ts_exchange = 5000;
    msg.order_id = 42;
    msg.instrument_id = 0;
    msg.canceled_qty = 100;

    auto parsed = Parser::parse(MsgType::CancelOrder,
        reinterpret_cast<const uint8_t*>(&msg), sizeof(msg));
    auto* cancel = std::get_if<ParsedCancel>(&parsed);
    ASSERT_TRUE(cancel != nullptr);
    ASSERT_EQ(cancel->msg.order_id, 42u);
}

TEST(parse_trade_valid) {
    TradeMsg msg{};
    msg.seq_no = 10;
    msg.ts_exchange = 10000;
    msg.order_id = 99;
    msg.instrument_id = 0;
    msg.price_ticks = 10050;
    msg.qty = 200;
    msg.side = Side::Ask;

    auto parsed = Parser::parse(MsgType::Trade,
        reinterpret_cast<const uint8_t*>(&msg), sizeof(msg));
    auto* trade = std::get_if<ParsedTrade>(&parsed);
    ASSERT_TRUE(trade != nullptr);
    ASSERT_EQ(trade->msg.qty, 200u);
}

TEST(parse_modify_order_valid) {
    ModifyOrderMsg msg{};
    msg.seq_no = 3;
    msg.order_id = 42;
    msg.new_price_ticks = 10100;
    msg.new_qty = 300;

    auto parsed = Parser::parse(MsgType::ModifyOrder,
        reinterpret_cast<const uint8_t*>(&msg), sizeof(msg));
    auto* mod = std::get_if<ParsedModify>(&parsed);
    ASSERT_TRUE(mod != nullptr);
    ASSERT_EQ(mod->msg.new_price_ticks, 10100u);
    ASSERT_EQ(mod->msg.new_qty, 300u);
}

TEST(parse_clear_valid) {
    ClearMsg msg{};
    msg.seq_no = 100;
    msg.instrument_id = 0;

    auto parsed = Parser::parse(MsgType::Clear,
        reinterpret_cast<const uint8_t*>(&msg), sizeof(msg));
    auto* clr = std::get_if<ParsedClear>(&parsed);
    ASSERT_TRUE(clr != nullptr);
}

TEST(parse_unknown_type) {
    auto parsed = Parser::parse(static_cast<MsgType>(99), nullptr, 0);
    auto* err = std::get_if<ParseError>(&parsed);
    ASSERT_TRUE(err != nullptr);
}

int main() {
    printf("Running parser tests...\n");
    return test::run_all();
}
