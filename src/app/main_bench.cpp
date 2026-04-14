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
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <feed.bin> [iterations]\n", argv[0]);
        return 1;
    }

    const char* feed_path = argv[1];
    int iterations = 10;
    if (argc >= 3) iterations = atoi(argv[2]);
    if (iterations < 1) iterations = 1;

    printf("Benchmark: %d iterations over %s\n\n", iterations, feed_path);

    StrategyConfig strat_cfg;
    strat_cfg.base_spread_ticks = 2;
    strat_cfg.lot_size = 100;
    strat_cfg.max_position = 1000;

    RiskLimits risk_limits{
        strat_cfg.max_position,
        strat_cfg.lot_size * 5,
        UINT64_MAX,
        100
    };

    uint64_t freq = tsc_frequency();
    printf("TSC frequency: %" PRIu64 " Hz\n\n", freq);

    struct IterResult {
        uint64_t msgs;
        double elapsed_ms;
        double throughput;
        int fills;
    };

    std::vector<IterResult> results;
    results.reserve(iterations);

    LatencyTracker best_lat;
    best_lat.set_tsc_freq(freq);

    for (int iter = 0; iter < iterations; ++iter) {
        ReplayReader reader;
        if (!reader.open(feed_path)) return 1;

        LatencyTracker lat;
        lat.set_tsc_freq(freq);

        OrderBook book(0);
        MarketMaker strategy(strat_cfg);
        RiskEngine risk(risk_limits);
        ExchangeSim exchange(1 << 20);
        OrderGateway gateway(exchange, 1 << 20);
        Portfolio portfolio(1);

        uint64_t msgs = 0;
        int fills = 0;
        auto t_start = rdtsc();

        while (auto raw = reader.next()) {
            msgs++;
            uint64_t t0 = rdtsc();
            auto parsed = Parser::parse(raw->type, raw->data, raw->length);
            uint64_t t1 = rdtsc();
            lat.record(LatencyTracker::MdIngress, t0, t1);

            std::optional<BboUpdate> bbo_update;

            if (auto* add = std::get_if<ParsedAdd>(&parsed)) {
                bbo_update = book.add_order(add->msg.order_id, add->msg.price_ticks,
                                            add->msg.qty, add->msg.side);
            } else if (auto* mod = std::get_if<ParsedModify>(&parsed)) {
                bbo_update = book.modify_order(mod->msg.order_id,
                                               mod->msg.new_price_ticks, mod->msg.new_qty);
            } else if (auto* cancel = std::get_if<ParsedCancel>(&parsed)) {
                bbo_update = book.cancel_order(cancel->msg.order_id);
            } else if (auto* trade = std::get_if<ParsedTrade>(&parsed)) {
                bbo_update = book.apply_trade(trade->msg.order_id, trade->msg.qty);
            } else if (auto* clr = std::get_if<ParsedClear>(&parsed)) {
                book.clear();
            } else {
                continue;
            }

            uint64_t t2 = rdtsc();
            lat.record(LatencyTracker::BookUpdate, t1, t2);

            if (!bbo_update) continue;

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

                auto fill_list = gateway.submit_intent(intent);
                uint64_t t5 = rdtsc();
                lat.record(LatencyTracker::Gateway, t4, t5);

                for (auto& fill : fill_list) {
                    portfolio.apply_fill(fill.side, fill.fill_qty, fill.fill_price);
                    strategy.on_fill(fill.side, fill.fill_qty);
                    fills++;
                }
            }
        }

        auto t_end = rdtsc();
        double elapsed_ns = tsc_to_ns(t_end - t_start);
        double elapsed_ms = elapsed_ns / 1e6;
        double throughput = static_cast<double>(msgs) / (elapsed_ns / 1e9);

        results.push_back({msgs, elapsed_ms, throughput, fills});
        printf("  iter %2d: %" PRIu64 " msgs, %.2f ms, %.0f msgs/sec, %d fills\n",
               iter, msgs, elapsed_ms, throughput, fills);

        if (iter == iterations - 1) {
            best_lat = std::move(lat);
        }
    }

    double total_ms = 0;
    double total_throughput = 0;
    for (auto& r : results) {
        total_ms += r.elapsed_ms;
        total_throughput += r.throughput;
    }

    printf("\n=== Benchmark Summary ===\n");
    printf("Avg elapsed:    %.2f ms\n", total_ms / iterations);
    printf("Avg throughput: %.0f msgs/sec\n", total_throughput / iterations);
    printf("Best throughput: %.0f msgs/sec\n",
           std::max_element(results.begin(), results.end(),
                            [](const auto& a, const auto& b) {
                                return a.throughput < b.throughput;
                            })->throughput);

    best_lat.print_summary();

    return 0;
}
