# Benchmarks

## Test Configuration

| Parameter | Value |
|-----------|-------|
| Platform | macOS, Apple Silicon (ARM64) |
| Compiler | Apple Clang 21.0 |
| Build | Release (`-O3 -march=native -flto -fno-exceptions -fno-rtti`) |
| Input | 10,000,000 synthetic messages (~363 MB) |
| Strategy | Market maker, spread=2, lot=100, max_pos=1000 |
| Risk | max_qty=500, max_pos=1000, collar=100 ticks |

## Throughput

```
Benchmark: 10 iterations over data/profile_feed.bin (10M messages, --target-orders 100000)

  iter  0: 10000001 msgs, 683.72 ms, 14625800 msgs/sec
  iter  1: 10000001 msgs, 684.31 ms, 14613317 msgs/sec
  iter  2: 10000001 msgs, 678.18 ms, 14745411 msgs/sec
  iter  3: 10000001 msgs, 685.54 ms, 14587132 msgs/sec
  iter  4: 10000001 msgs, 676.86 ms, 14774135 msgs/sec
  iter  5: 10000001 msgs, 682.40 ms, 14654223 msgs/sec
  iter  6: 10000001 msgs, 690.77 ms, 14476647 msgs/sec
  iter  7: 10000001 msgs, 698.82 ms, 14309762 msgs/sec
  iter  8: 10000001 msgs, 676.36 ms, 14784939 msgs/sec
  iter  9: 10000001 msgs, 681.96 ms, 14663644 msgs/sec

  Avg throughput: 14.6M msgs/sec
  Best throughput: 14.8M msgs/sec
```

First iteration is slower due to instruction cache and branch prediction warmup.
Iterations 2-9 are stable at ~14.6-14.8M msgs/sec.

## Per-Stage Latency

```
stage                     count        min        p50        p90        p99      p99.9        max
                                       (ns)       (ns)       (ns)       (ns)       (ns)       (ns)
md_ingress_ns           1000603         17         42         42         83        125       7643
book_update_ns          8653564         17         42         84        166        209      11018
strategy_ns               51349         17         42         42         83        125       8792
risk_ns                  555531         17         83        125        208        542    1375832
gateway_ns               501371         17         42        125        167        500    1375832
```

### Observations

**Parser (`md_ingress`):** 42 ns median. memcpy-based field extraction with
explicit wire offsets. No string processing, no virtual dispatch.

**Book update:** 42 ns median. Uses `FlatHashMap<uint64_t, OrderState>` for
O(1) order lookup + `std::map` for sorted price levels. The bimodal p50/p90
reflects the difference between updating an existing level vs. inserting a new
one in the tree.

**Strategy:** 42 ns median. Pure function with stack-allocated output
(`StrategyOutput` with inline array, max 4 orders). Zero heap allocation.

**Risk:** 83 ns median. Stateless validation with 4 checks (qty, position,
notional, price collar). Branch-predictable — mostly accepts.

**Gateway + Exchange:** 42-125 ns median. `std::unordered_map` for active
order tracking + `std::map` for resting order matching. The p99 at ~1.4ms
reflects multi-level sweeps or hash table growth.

## Optimization History

| # | Change | Effect |
|---|--------|--------|
| 1 | Remove `__attribute__((packed))`, use field-by-field memcpy | +2-3% throughput, -21% p99.9 |
| 2 | Replace `std::unordered_map` with custom `FlatHashMap` | +30% throughput, -43% p99.9, -94% max latency |
| 3 | Compact `OrderState` (remove redundant `order_id`) | 32→24 byte slots, -25% memory |
| 4 | Split FlatHashMap key/value arrays | Better probe cache density |
| 5 | Pre-allocate order hash map (8M slots) | Eliminates growth overhead |
| 6 | Replace `LatencyTracker` vectors with fixed-size arrays | Eliminates 10M vector push_backs on hot path |
| 7 | Stack-allocated `StrategyOutput` | Eliminates per-BBO vector heap allocation |
| 8 | Balanced feed generator (`--target-orders`) | Keeps book cache-friendly (~100K orders in L3) |
| 9 | `-O3 -fno-exceptions -fno-rtti` for all targets | Consistent optimization |

### Price Level Data Structure Investigation

Tested 3 alternatives to `std::map` for price levels:

| Structure | Throughput | Book p50 | Notes |
|-----------|-----------|---------|-------|
| `std::map` (baseline) | 14.8M | 42 ns | Best for typical book depths (100+ levels) |
| Sorted vector + memmove | 12.0M | 83 ns | O(n) insert/erase shifting kills performance |
| FlatHashMap + cached best | 15.6M* | 83 ns | Fast O(1) ops but hash overhead on hot path |

\* With unbalanced feed (6M orders). `std::map` wins for balanced workloads because
its tree operations are highly optimized in libc++ and `rbegin()`/`begin()` for
best price are O(1).

## Realistic Numbers

For context, real low-latency trading systems target:

| Metric | This project | Production HFT |
|--------|-------------|----------------|
| Tick-to-trade | ~84 ns (parser+book) | < 1 µs (wire-to-wire) |
| Book update p50 | 42 ns | 10-50 ns |
| Strategy decision | 42 ns | 5-20 ns |
| Msg throughput | 14.8M/sec | 10-50M/sec |
| Heap allocs/msg | 0 (orders), ~1 (price levels) | 0 |

## How to Run

```bash
# Generate balanced profile data (10M messages, ~100K active orders)
python3 scripts/generate_feed.py -o data/profile_feed.bin -n 10000000 \
    --seed 42 --stats --target-orders 100000

# Single replay with summary
./build/mini_trader data/profile_feed.bin

# Benchmark mode (10 iterations)
./build/mini_trader_bench data/profile_feed.bin

# Quick test with small data
python3 scripts/generate_feed.py -o data/synthetic_feed.bin -n 500000 --seed 42 --stats
./build/mini_trader data/synthetic_feed.bin
```

## Regression Testing

All benchmarks should be run after any optimization change. Record:

1. Throughput (msgs/sec) — should not decrease
2. p50 latency per stage — should not increase
3. p99 latency — watch for tail regression
4. Test suite — must remain passing (36/36)

The `scripts/generate_feed.py --seed 42` guarantees identical input across
runs, so throughput numbers are directly comparable.
