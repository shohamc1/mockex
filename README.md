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

Supports two feed formats:
- **Native** — compact little-endian binary protocol with 3-byte message headers
- **ITCH 5.0** — direct NASDAQ TotalView ITCH parsing at runtime (big-endian, 6-byte timestamps, order-state tracking)

Both paths are latency-instrumented. The ITCH path includes the feed handler as a measured pipeline stage.

All latency is measured with `rdtsc` (x86) / `cntvct_el0` (ARM), reported as p50/p90/p99/p99.9/max.

### Components

| Component | File | Description |
|-----------|------|-------------|
| Feed Handler (native) | `src/feed/parser.hpp` | Internal binary protocol parser |
| Feed Handler (ITCH) | `src/feed/itch_parser.hpp` | NASDAQ ITCH 5.0 parser with order-state tracking |
| Replay Reader (native) | `src/feed/replay_reader.hpp` | Memory-mapped native format reader |
| Replay Reader (ITCH) | `src/feed/itch_replay_reader.hpp` | Memory-mapped ITCH format reader |
| Order Book | `src/book/order_book.hpp` | Level 2 aggregated book with cached BBO, per-order ID tracking |
| Strategy | `src/strategy/market_maker.hpp` | Market-making with inventory skew |
| Risk Engine | `src/risk/risk_engine.hpp` | Max qty, position, notional, price collar checks |
| Order Gateway | `src/gateway/order_gateway.hpp` | Order ID assignment, fill tracking |
| Exchange Sim | `src/exchange/exchange_sim.hpp` | Price-time priority matching engine |
| Portfolio | `src/portfolio/portfolio.hpp` | Position, cash, PnL tracking |

### Design Choices

- **Cached BBO** — best bid/ask price+qty cached in 4 `uint32_t` fields, eliminating redundant `std::map` tree traversals
- **mmap I/O** — feed files are memory-mapped, no `fread` + heap buffer copy
- **Custom `FlatHashMap`** — open addressing, linear probing, backward-shift deletion, Fibonacci hashing. Pre-sized to avoid allocation on hot path
- **Output-param parser** — ITCH parser returns enum + union instead of `std::optional<std::variant<...>>`, avoiding 48+ byte object construction per message
- **`std::map` for price levels** — `rbegin()`/`begin()` for best price are O(1); BBO cache avoids repeated lookups
- **Stack-allocated result arrays** — `StrategyOutput`, `ExecutionReports`, `FillResults` use fixed-size inline arrays (max 4) instead of `std::vector`
- **Zero virtual dispatch on hot path** — no virtual functions in the message processing loop
- **Binary wire protocol** — no JSON/text parsing on the hot path
- **Deterministic replay** — same input always produces the same output
- **No external dependencies** — core pipeline uses only the standard library

## Performance

Benchmarked on Apple Silicon (ARM64), 10M messages, ~100K active orders, Release build (`-O3 -march=native -flto`):

### Native format

```
Avg throughput:     16.5M msgs/sec
Best throughput:    16.6M msgs/sec

stage            count         p50        p90        p99       p99.9        max
                              (ns)       (ns)       (ns)       (ns)       (ns)
md_ingress     ~1.2M           42         42        500        958     10.7µs
book_update    ~7.9M           42         84        125        208     11.9µs
strategy        ~71K           42         83         84        208      4.9µs
risk           ~466K           42        125        167        250     11.0µs
gateway        ~364K           42        125        208        291     11.0µs
```

### ITCH 5.0 format (direct parsing)

```
Avg throughput:     12.8M msgs/sec
Best throughput:    13.7M msgs/sec

stage            count         p50        p90        p99       p99.9        max
                              (ns)       (ns)       (ns)       (ns)       (ns)
md_ingress     ~1.8M           42         42        167      1.0µs     12.0µs
book_update    ~8.0M           42        125        167        250     14.6µs
strategy          ~70          42         42        125        125        167
risk             ~439          42        125        208        250        584
gateway          ~812          42         83        166        375      1.5µs
```

The ITCH path is ~23% slower than native due to big-endian byte swaps, 6-byte timestamp extraction, and hash map lookups for order-state tracking on E/X/D messages. This measures the full feed handler cost, not just "book update onward."

## Build & Run

**Prerequisites:** C++20 compiler, CMake 3.20+, Python 3

```bash
# Build (Release mode enables -O3 -march=native -flto -fno-exceptions -fno-rtti)
cmake -B build -DCMAKE_BUILD_TYPE=Release .
cmake --build build -j$(nproc)

# Run tests (36 tests)
./build/test_book && ./build/test_parser && ./build/test_risk && ./build/test_exchange

# Generate synthetic native feed and run end-to-end
python3 scripts/generate_feed.py -o data/synthetic_feed.bin -n 500000 --stats
./build/mini_trader data/synthetic_feed.bin

# Generate synthetic ITCH feed and run with direct ITCH parsing
python3 scripts/generate_itch_feed.py -o data/synthetic_itch.bin -n 500000 --stats
./build/mini_trader data/synthetic_itch.bin --format itch

# Benchmark native format (multi-iteration with latency breakdown)
python3 scripts/generate_feed.py -o data/bench_feed.bin -n 10000000 \
    --target-orders 100000 --seed 42 --stats
./build/mini_trader_bench data/bench_feed.bin 5

# Benchmark ITCH format
python3 scripts/generate_itch_feed.py -o data/bench_itch.bin -n 10000000 \
    --target-orders 100000 --seed 42 --stats
./build/mini_trader_bench data/bench_itch.bin 5 --format itch
```

**One-liner:** `bash scripts/run_bench.sh` — builds, generates feed, runs replay + benchmark.

## ITCH 5.0 Support

The pipeline can consume ITCH 5.0 data in two ways:

### 1. Direct runtime parsing (recommended)

```bash
# Generate synthetic ITCH data
python3 scripts/generate_itch_feed.py -o data/synthetic_itch.bin -n 1000000 --stats

# Parse ITCH directly in the C++ pipeline
./build/mini_trader data/synthetic_itch.bin --format itch

# With locate code filtering
./build/mini_trader data/synthetic_itch.bin --format itch --locate 1
```

### 2. Offline conversion

```bash
# Convert ITCH to internal format first
python3 scripts/itch_to_feed.py -i data/synthetic_itch.bin -o data/converted.bin --stats

# Then run with native parser
./build/mini_trader data/converted.bin
```

### Real NASDAQ data

```bash
# Download sample data (3.5GB)
bash scripts/download_sample.sh

# Parse directly
./build/mini_trader data/12302019.NASDAQ_ITCH50 --format itch

# Or convert offline
python3 scripts/itch_to_feed.py data/12302019.NASDAQ_ITCH50 -o data/itch_feed.bin
./build/mini_trader data/itch_feed.bin
```

### ITCH message types supported

| ITCH Type | Description | Internal Mapping |
|-----------|-------------|-----------------|
| `A` | Add Order (no MPID) | Add |
| `F` | Add Order (with MPID) | Add |
| `E` | Order Executed | Trade (lookup order state) |
| `C` | Order Executed with Price | Trade (lookup order state) |
| `X` | Order Cancel | Cancel (partial) |
| `D` | Order Delete | Cancel (full) |
| `U` | Order Replace | Modify (tracks new order ID) |
| `P` | Trade (non-cross) | Trade |
| `S` | System Event | Skipped |
| `R` | Stock Directory | Skipped (used for locate mapping in converter) |

## CLI Flags

```
mini_trader <feed.bin> [options]
  --format native|itch  feed format (default: native)
  --max-msgs N          process at most N messages
  --spread N            base spread in ticks (default 2)
  --lot-size N          lot size (default 100)
  --max-pos N           max position (default 1000)
  --locate N            ITCH locate code filter (0=all)
  --verbose             print every fill

mini_trader_bench <feed.bin> [iterations] [--format native|itch] [--locate N]
```

## Project Structure

```
src/
  app/          main_replay.cpp, main_bench.cpp
  book/         order_book.hpp
  common/       types.hpp, flat_hash_map.hpp, latency_tracker.hpp, time.hpp, ring_buffer.hpp
  exchange/     exchange_sim.hpp
  feed/         replay_reader.hpp/.cpp, parser.hpp/.cpp
                itch_replay_reader.hpp/.cpp, itch_parser.hpp/.cpp
  gateway/      order_gateway.hpp
  portfolio/    portfolio.hpp
  risk/         risk_engine.hpp
  strategy/     market_maker.hpp
tests/          36 unit tests (book: 14, parser: 8, risk: 7, exchange: 7)
scripts/        generate_feed.py, generate_itch_feed.py, itch_to_feed.py,
                download_sample.sh, run_bench.sh
docs/           architecture.md, benchmarks.md, protocol.md
```
