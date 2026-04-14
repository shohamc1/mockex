# mockex — Mini Trading Stack

A high-performance C++20 trading pipeline that processes binary market data through an order book, strategy, risk engine, order gateway, and exchange simulator — with per-stage latency instrumentation.

Built to demonstrate understanding of the full path from market data arrival to order transmission to fill handling. Performance is the top priority.

## Architecture

Single-threaded, replay-first design. Every message flows through the same pipeline:

```
Binary Feed → Parser → Order Book → Strategy → Risk Engine → Order Gateway → Exchange Sim
                                                                                   ↓
                                                                              Execution Reports
                                                                                   ↓
                                                                              Portfolio / Fill Log
```

All latency is measured with `rdtsc` (x86) / `cntvct_el0` (ARM), reported as p50/p90/p99/p99.9/max.

### Components

| Component | File | Description |
|-----------|------|-------------|
| Feed Handler | `src/feed/` | Binary wire protocol parser, field-by-field memcpy |
| Order Book | `src/book/order_book.hpp` | Level 2 aggregated book with per-order ID tracking |
| Strategy | `src/strategy/market_maker.hpp` | Market-making with inventory skew |
| Risk Engine | `src/risk/risk_engine.hpp` | Max qty, position, notional, price collar checks |
| Order Gateway | `src/gateway/order_gateway.hpp` | Order ID assignment, fill tracking |
| Exchange Sim | `src/exchange/exchange_sim.hpp` | Price-time priority matching engine |
| Portfolio | `src/portfolio/portfolio.hpp` | Position, cash, PnL tracking |

### Design Choices

- **Zero allocation on hot path** — all containers pre-sized, no `std::vector` returns, no virtual dispatch
- **Custom `FlatHashMap`** — open addressing, linear probing, backward-shift deletion, Fibonacci hashing. Replaces `std::unordered_map` everywhere on the hot path (3x faster in practice)
- **`std::map` for price levels** — benchmarked against sorted vectors, flat arrays, and hash maps. libc++'s red-black tree wins because `rbegin()`/`begin()` for best price are O(1)
- **Stack-allocated result arrays** — `StrategyOutput`, `ExecutionReports`, `FillResults` use fixed-size inline arrays (max 4) instead of `std::vector`
- **Binary wire protocol** — no JSON/text parsing on the hot path
- **Deterministic replay** — same input always produces the same output
- **No external dependencies** — core pipeline uses only the standard library

## Performance

Benchmarked on Apple Silicon (ARM64), 10M messages, ~100K active orders, Release build (`-O3 -march=native -flto`):

```
Avg throughput:     14.5M msgs/sec
Best throughput:    14.9M msgs/sec

stage          count         p50        p90        p99       p99.9        max
                             (ns)       (ns)       (ns)       (ns)       (ns)
parser         ~1M            42         42         83        125      2.8µs
book           ~8.7M          42        125        167        291     13.5µs
strategy       ~55K           42         42         84        167        417
risk           ~540K          42        125        208        292     12.9µs
gateway        ~370K          42        125        208        333     12.8µs
```

## Build & Run

**Prerequisites:** C++20 compiler, CMake 3.20+, Python 3

```bash
# Build (Release mode enables -O3 -march=native -flto -fno-exceptions -fno-rtti)
cmake -B build -DCMAKE_BUILD_TYPE=Release .
cmake --build build -j$(nproc)

# Run tests (36 tests)
./build/test_book && ./build/test_parser && ./build/test_risk && ./build/test_exchange

# Generate synthetic feed and run end-to-end
python3 scripts/generate_feed.py -o data/synthetic_feed.bin -n 500000 --stats
./build/mini_trader data/synthetic_feed.bin

# Benchmark (multi-iteration with latency breakdown)
./build/mini_trader_bench data/synthetic_feed.bin 5

# Generate a larger feed for profiling (10M msgs, ~100K active orders)
python3 scripts/generate_feed.py -o data/profile_feed.bin -n 10000000 \
    --target-orders 100000 --seed 42 --stats
./build/mini_trader_bench data/profile_feed.bin 10
```

**One-liner:** `bash scripts/run_bench.sh` — builds, generates feed, runs replay + benchmark.

## ITCH 5.0 Support

A converter for real NASDAQ TotalView ITCH 5.0 data is included:

```bash
# Download sample data (3.5GB)
bash scripts/download_sample.sh

# Convert to internal binary format
python3 scripts/itch_to_feed.py data/12302019.NASDAQ_ITCH50 -o data/itch_feed.bin

# Replay
./build/mini_trader data/itch_feed.bin
```

## Project Structure

```
src/
  app/          main_replay.cpp, main_bench.cpp
  book/         order_book.hpp
  common/       types.hpp, flat_hash_map.hpp, latency_tracker.hpp, time.hpp, ring_buffer.hpp
  exchange/     exchange_sim.hpp
  feed/         replay_reader.hpp/.cpp, parser.hpp/.cpp
  gateway/      order_gateway.hpp
  portfolio/    portfolio.hpp
  risk/         risk_engine.hpp
  strategy/     market_maker.hpp
tests/          36 unit tests (book: 14, parser: 8, risk: 7, exchange: 7)
scripts/        generate_feed.py, itch_to_feed.py, download_sample.sh, run_bench.sh
docs/           architecture.md, benchmarks.md, protocol.md
```
