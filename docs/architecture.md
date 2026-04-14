# Architecture

## Overview

A single-threaded, replay-driven, event-driven trading stack that processes
binary market data through an order book, generates quoting decisions via a
market-making strategy, validates them through a risk engine, routes them
through a gateway to a simulated exchange, and tracks portfolio state — all
with per-stage latency instrumentation.

```
[Replay File]
      |
      v
[ReplayReader] → raw bytes
      |
      v
[Parser] → typed message structs
      |
      v
[OrderBook] → BboUpdate (on BBO change)
      |
      v
[MarketMaker] → vector<OrderIntent>
      |
      v
[RiskEngine] → Accept / Reject
      |
      v
[OrderGateway] → OutboundOrder
      |
      v
[ExchangeSim] → ExecutionReport(s)
      |
      v
[Portfolio] → position, cash, PnL
```

## Threading Model

Phase 1 (current): single-threaded. Every stage runs sequentially in one loop.
This guarantees determinism and makes the latency measurements clean — no
queue wait times polluting the numbers.

Phase 2 (planned): two threads connected by SPSC ring buffers.

```
Thread 1: Feed + Book + Strategy
  ReplayReader → Parser → OrderBook → MarketMaker → [SPSC] →
                                                           |
Thread 2: Gateway + Exchange                              v
  [SPSC] → OrderGateway → ExchangeSim → [SPSC fills] → Portfolio
```

The `SPSCRingBuffer<T, Capacity>` is already implemented in
`common/ring_buffer.hpp`. It is power-of-2 sized, cache-line padded, and uses
`std::atomic` with explicit memory ordering (acquire/release).

## Component Details

### Feed Handler

**ReplayReader** (`feed/replay_reader.hpp`) memory-maps a binary file into a
`std::vector<uint8_t>` and yields messages one at a time via `next()`. Each
message is a `RawMsg` containing a `MsgType` tag, a pointer into the buffer,
and a length — zero-copy until the parser.

Sequence gap detection: each message carries a `seq_no`. The reader tracks
`last_seq_no_` and increments `gap_count_` when a discontinuity is detected.

**Parser** (`feed/parser.hpp`) interprets raw bytes into typed POD structs
(`AddOrderMsg`, `CancelOrderMsg`, etc.) via `memcpy`. Returns a
`std::variant` of parsed types or `ParseError`. Validates: non-zero qty/price,
valid side, sufficient length.

### Order Book

**OrderBook** (`book/order_book.hpp`) is Level 2 aggregated with per-order
tracking:

- `std::map<uint32_t, uint32_t> bids_` — price → aggregated qty, sorted
  descending (via `rbegin()`)
- `std::map<uint32_t, uint32_t> asks_` — price → aggregated qty, sorted
  ascending
- `std::unordered_map<uint64_t, OrderState> orders_` — order ID → {price,
  qty, side}

This hybrid approach gives us:
- Aggregated L2 view for strategy signals
- Per-order cancel/modify support via the orders_ map
- O(log N) BBO access via `std::map`

`maybe_bbo_update()` compares old and new BBO prices *and quantities* — it
only returns a `BboUpdate` when something at the top actually changed. This
avoids unnecessary strategy invocations.

Known allocation points:
- `std::map` node allocation on new price levels
- `std::unordered_map` bucket allocation on new orders

These are the Phase 3 optimization targets (flat arrays, pool allocators).

### Strategy

**MarketMaker** (`strategy/market_maker.hpp`) is a pure function: given a
`BboUpdate` and internal inventory state, it emits a `vector<OrderIntent>`.

Logic:
```
fair  = (best_bid + best_ask) / 2
skew  = inventory * risk_skew_factor
bid_px = fair - base_spread - skew
ask_px = fair + base_spread - skew
```

Position management:
- If inventory >= max_position, pull bid (stop buying)
- If inventory <= -max_position, pull ask (stop selling)

The strategy is deterministic: same BBO + same inventory → same output.
No randomness, no timers, no external state.

### Risk Engine

**RiskEngine** (`risk/risk_engine.hpp`) is a stateless validator. Each call to
`validate()` checks:

| Check | Rule |
|-------|------|
| Max order qty | `order.qty <= limits.max_order_qty` |
| Max position | `|new_position| <= limits.max_position` |
| Max notional | `exposure + new_notional <= limits.max_notional` |
| Price collar | `|order.price - ref_price| <= limits.price_collar_ticks` |

Returns `RiskResult` enum. Tracks total checks and reject count in mutable
stats (safe because single-threaded).

### Order Gateway

**OrderGateway** (`gateway/order_gateway.hpp`) sits between strategy and
exchange. Responsibilities:

- Assigns monotonically increasing `client_order_id`
- Converts `OrderIntent` → `OutboundOrder`
- Maintains `active_orders_` map for fill reconciliation
- Processes `ExecutionReport`s: updates active orders, extracts `FillInfo`
  for portfolio

### Exchange Simulator

**ExchangeSim** (`exchange/exchange_sim.hpp`) implements simple price-time
priority matching:

- Incoming buy order sweeps ask levels from best ask upward
- Incoming sell order sweeps bid levels from best bid downward
- Unfilled quantity rests in a `std::map` of price → `RestingOrder`
- Supports cancel (removes from book)

Emits: Ack, Fill, PartialFill, Cancelled execution reports.

### Portfolio

**Portfolio** (`portfolio/portfolio.hpp`) tracks:
- Position (signed quantity)
- Cash (in cents)
- Notional exposure

`apply_fill()` updates position and cash. `snapshot()` returns a
`PortfolioState` for risk checks. `unrealized_pnl()` computes mark-to-market
using current midprice.

## Latency Measurement

`LatencyTracker` (`common/latency_tracker.hpp`) records cycle counts at each
pipeline stage using `rdtsc()` (x86) or `cntvct_el0` (ARM). At the end of
replay, it converts to nanoseconds and prints percentiles.

Labels recorded per message:

| Label | Start | End |
|-------|-------|-----|
| `md_ingress` | message read | post-parse |
| `book_update` | post-parse | post-book-mutation |
| `strategy` | post-book | post-strategy-decision |
| `risk` | post-strategy | post-risk-check |
| `gateway` | post-risk | post-exchange-response |
| `tick_to_ack` | reserved for cross-thread measurement |

Timestamps use `rdtsc()` directly — no system calls on the hot path.

## Determinism

Given the same input file and configuration, the system produces identical:
- Fill log
- Position trajectory
- PnL
- Order decisions
- Latency sample counts

The synthetic feed generator (`scripts/generate_feed.py`) accepts `--seed` for
reproducible data. The ITCH converter (`scripts/itch_to_feed.py`) is
deterministic by nature (reads a fixed binary file).

## Data Flow: Zero-Copy Points

1. `ReplayReader` reads file into one contiguous buffer
2. `RawMsg` points into that buffer — no copy
3. `Parser::parse()` does one `memcpy` from buffer into a stack-local struct
4. That struct is accessed by pointer via `std::get_if` — no copy
5. `BboUpdate` is a 24-byte value type — one copy from book to strategy
6. `OrderIntent` is a 32-byte value type — copied into gateway
7. `ExecutionReport` flows back as a value type

The only heap allocations on the hot path are:
- `std::vector<OrderIntent>` returned by strategy (reserved to 4)
- `std::vector<ExecutionReport>` returned by exchange sim (reserved to 2)
- `std::map`/`std::unordered_map` node insertion in order book and exchange sim

## Future Work

See `docs/benchmarks.md` for optimization notes and target numbers.
