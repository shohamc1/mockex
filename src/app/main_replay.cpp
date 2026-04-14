#include "common/types.hpp"
#include "common/time.hpp"
#include "common/latency_tracker.hpp"
#include "feed/replay_reader.hpp"
#include "feed/parser.hpp"
#include "book/order_book.hpp"
#include "strategy/market_maker.hpp"
#include "risk/risk_engine.hpp"
#include "gateway/order_gateway.hpp"
#include "exchange/exchange_sim.hpp"
#include "portfolio/portfolio.hpp"
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>

struct FillLogEntry {
    uint64_t seq_no;
    Side side;
    uint32_t qty;
    uint32_t price;
};

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <feed.bin> [options]\n", argv[0]);
        fprintf(stderr, "  --max-msgs N      process at most N messages\n");
        fprintf(stderr, "  --spread N        base spread in ticks (default 2)\n");
        fprintf(stderr, "  --lot-size N      lot size (default 100)\n");
        fprintf(stderr, "  --max-pos N       max position (default 1000)\n");
        fprintf(stderr, "  --verbose         print every fill\n");
        return 1;
    }

    const char* feed_path = argv[1];
    uint64_t max_msgs = UINT64_MAX;
    bool verbose = false;

    StrategyConfig strat_cfg;
    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--max-msgs") == 0 && i + 1 < argc) {
            max_msgs = strtoull(argv[++i], nullptr, 10);
        } else if (strcmp(argv[i], "--spread") == 0 && i + 1 < argc) {
            strat_cfg.base_spread_ticks = static_cast<uint32_t>(atoi(argv[++i]));
        } else if (strcmp(argv[i], "--lot-size") == 0 && i + 1 < argc) {
            strat_cfg.lot_size = static_cast<uint32_t>(atoi(argv[++i]));
        } else if (strcmp(argv[i], "--max-pos") == 0 && i + 1 < argc) {
            strat_cfg.max_position = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
        }
    }

    RiskLimits risk_limits{
        strat_cfg.max_position,
        strat_cfg.lot_size * 5,
        UINT64_MAX,
        100
    };

    ReplayReader reader;
    if (!reader.open(feed_path)) {
        return 1;
    }

    LatencyTracker lat;
    lat.set_tsc_freq(tsc_frequency());

    OrderBook book(0);
    MarketMaker strategy(strat_cfg);
    RiskEngine risk(risk_limits);
    ExchangeSim exchange;
    OrderGateway gateway(exchange);
    Portfolio portfolio(1);

    std::vector<FillLogEntry> fill_log;
    fill_log.reserve(1 << 16);

    uint64_t msgs_processed = 0;
    uint64_t last_seq = 0;

    auto t_start = rdtsc();

    while (auto raw = reader.next()) {
        if (msgs_processed >= max_msgs) break;
        msgs_processed++;

        uint64_t t0 = rdtsc();

        auto parsed = Parser::parse(raw->type, raw->data, raw->length);
        uint64_t t1 = rdtsc();
        lat.record(LatencyTracker::MdIngress, t0, t1);

        std::optional<BboUpdate> bbo_update;

        if (auto* add = std::get_if<ParsedAdd>(&parsed)) {
            last_seq = add->msg.seq_no;
            bbo_update = book.add_order(add->msg.order_id, add->msg.price_ticks,
                                        add->msg.qty, add->msg.side);
        } else if (auto* mod = std::get_if<ParsedModify>(&parsed)) {
            last_seq = mod->msg.seq_no;
            bbo_update = book.modify_order(mod->msg.order_id,
                                           mod->msg.new_price_ticks, mod->msg.new_qty);
        } else if (auto* cancel = std::get_if<ParsedCancel>(&parsed)) {
            last_seq = cancel->msg.seq_no;
            bbo_update = book.cancel_order(cancel->msg.order_id);
        } else if (auto* trade = std::get_if<ParsedTrade>(&parsed)) {
            last_seq = trade->msg.seq_no;
            bbo_update = book.apply_trade(trade->msg.order_id, trade->msg.qty);
        } else if (auto* clr = std::get_if<ParsedClear>(&parsed)) {
            last_seq = clr->msg.seq_no;
            book.clear();
        } else {
            continue;
        }

        uint64_t t2 = rdtsc();
        lat.record(LatencyTracker::BookUpdate, t1, t2);

        if (!bbo_update) continue;

        bbo_update->ts_local = t2;

        auto intents = strategy.on_bbo(*bbo_update);
        uint64_t t3 = rdtsc();
        lat.record(LatencyTracker::Strategy, t2, t3);

        for (auto& intent : intents) {
            uint32_t ref_price = static_cast<uint32_t>(book.midprice());
            auto snap = portfolio.snapshot(ref_price);

            auto rr = risk.validate(intent, snap, ref_price);
            uint64_t t4 = rdtsc();
            lat.record(LatencyTracker::Risk, t3, t4);

            if (rr != RiskResult::Accept) continue;

            auto fills = gateway.submit_intent(intent);
            uint64_t t5 = rdtsc();
            lat.record(LatencyTracker::Gateway, t4, t5);

            for (auto& fill : fills) {
                portfolio.apply_fill(fill.side, fill.fill_qty, fill.fill_price);
                strategy.on_fill(fill.side, fill.fill_qty);
                fill_log.push_back({last_seq, fill.side, fill.fill_qty, fill.fill_price});

                if (verbose) {
                    printf("FILL seq=%" PRIu64 " %s qty=%u px=%u pos=%d cash=%" PRId64 "\n",
                           last_seq,
                           fill.side == Side::Bid ? "BUY " : "SELL",
                           fill.fill_qty, fill.fill_price,
                           portfolio.position(), portfolio.cash());
                }
            }
        }
    }

    auto t_end = rdtsc();
    double elapsed_ns = tsc_to_ns(t_end - t_start);

    printf("\n=== Replay Summary ===\n");
    printf("Messages processed: %" PRIu64 "\n", msgs_processed);
    printf("Sequence gaps:      %" PRIu64 "\n", reader.gap_count());
    printf("Fills:              %zu\n", fill_log.size());
    printf("Position:           %d\n", portfolio.position());
    printf("Cash:               %" PRId64 " cents\n", portfolio.cash());
    if (book.is_valid()) {
        printf("Final midprice:     %" PRId64 "\n", book.midprice());
        printf("Unrealized PnL:     %" PRId64 " cents\n",
               portfolio.unrealized_pnl(static_cast<uint32_t>(book.midprice())));
    }
    printf("Elapsed:            %.2f ms\n", elapsed_ns / 1e6);
    printf("Throughput:         %.0f msgs/sec\n",
           msgs_processed / (elapsed_ns / 1e9));
    printf("Risk rejects:       %" PRIu64 " / %" PRIu64 "\n",
           risk.stats().rejects, risk.stats().total_checks);
    printf("Gateway stats:      sent=%" PRIu64 " fills=%" PRIu64 " cancels=%" PRIu64 "\n",
           gateway.stats().orders_sent, gateway.stats().fills_received,
           gateway.stats().orders_cancelled);

    lat.print_summary();

    return 0;
}
