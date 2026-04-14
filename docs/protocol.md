# Internal Binary Protocol

## Wire Format

Each message on the wire consists of a 3-byte header followed by a typed
payload:

```
+--------+--------+--------+============================+
| type   | length (LE uint16) | payload                   |
| (1B)   | (2B)               | (variable)                |
+--------+--------+--------+============================+
```

All multi-byte fields are **little-endian**.

### Header

```cpp
struct __attribute__((packed)) WireHeader {
    MsgType type;      // 1 byte
    uint16_t length;   // payload length in bytes (not including header)
};
```

### Message Types

```cpp
enum class MsgType : uint8_t {
    AddOrder    = 0,
    ModifyOrder = 1,
    CancelOrder = 2,
    Trade       = 3,
    Clear       = 4
};
```

## Message Structures

### AddOrder (type = 0)

```
Offset  Size  Field
0       8     seq_no          (uint64_t)
8       8     ts_exchange     (uint64_t) nanoseconds since midnight
16      8     order_id        (uint64_t)
24      4     instrument_id   (uint32_t)
28      4     price_ticks     (uint32_t) price in tick units
32      4     qty             (uint32_t) quantity in shares/contracts
36      1     side            (uint8_t)  0 = Bid, 1 = Ask
Total: 37 bytes
```

### ModifyOrder (type = 1)

```
Offset  Size  Field
0       8     seq_no          (uint64_t)
8       8     ts_exchange     (uint64_t)
16      8     order_id        (uint64_t)
24      4     instrument_id   (uint32_t)
28      4     new_price_ticks (uint32_t)
32      4     new_qty         (uint32_t)
Total: 36 bytes
```

### CancelOrder (type = 2)

```
Offset  Size  Field
0       8     seq_no          (uint64_t)
8       8     ts_exchange     (uint64_t)
16      8     order_id        (uint64_t)
24      4     instrument_id   (uint32_t)
28      4     canceled_qty    (uint32_t)
Total: 32 bytes
```

### Trade (type = 3)

```
Offset  Size  Field
0       8     seq_no          (uint64_t)
8       8     ts_exchange     (uint64_t)
16      8     order_id        (uint64_t) resting order that was executed
24      4     instrument_id   (uint32_t)
28      4     price_ticks     (uint32_t) execution price
32      4     qty             (uint32_t) executed quantity
36      1     side            (uint8_t)  side of the resting order
Total: 37 bytes
```

### Clear (type = 4)

```
Offset  Size  Field
0       8     seq_no          (uint64_t)
8       8     ts_exchange     (uint64_t)
16      4     instrument_id   (uint32_t) instrument to clear
Total: 20 bytes
```

## Enum Values

```cpp
enum class Side : uint8_t {
    Bid = 0,
    Ask = 1
};

enum class OrderAction : uint8_t {
    New    = 0,
    Cancel = 1,
    Modify = 2
};

enum class ExecType : uint8_t {
    Ack         = 0,
    Reject      = 1,
    Fill        = 2,
    PartialFill = 3,
    Cancelled   = 4
};
```

## Price Convention

Prices are expressed as unsigned integer **ticks**. The conversion to/from
real currency depends on the instrument's tick size:

- For US equities (from ITCH): `price_ticks = itch_price_raw / 100`
  where `itch_price_raw` is in 1/10000ths of a dollar. So `price_ticks` is
  in cents (1 tick = $0.01).
- For synthetic data: ticks are arbitrary units.

The converter (`scripts/itch_to_feed.py`) handles this transformation at
conversion time, so the C++ pipeline always works in ticks.

## Timestamp Convention

- `ts_exchange`: nanoseconds since midnight (from exchange clock or simulated)
- `ts_local`: TSC cycles from `rdtsc()`, converted to ns at reporting time

The pipeline uses three time domains:
1. **Exchange time** (`ts_exchange`): when the event happened at the exchange
2. **Local TSC** (`ts_local` in BboUpdate): rdtsc at the moment of book update
3. **Latency deltas**: difference between consecutive rdtsc readings per stage

## File Layout

A feed file is a concatenation of messages with no framing beyond the per-
message header:

```
[WireHeader][AddOrderMsg payload][WireHeader][CancelOrderMsg payload]...
```

The `ReplayReader` reads the entire file into memory and walks through it
sequentially. No seeking, no indexing.

## ITCH 5.0 Adapter

The ITCH converter (`scripts/itch_to_feed.py`) maps NASDAQ TotalView ITCH 5.0
messages to our internal format:

| ITCH Message | Code | Internal Message | Notes |
|-------------|------|-----------------|-------|
| Add Order (no MPID) | `A` | AddOrder | Direct mapping |
| Add Order (with MPID) | `F` | AddOrder | MPID discarded |
| Order Executed | `E` | Trade | Uses resting order's price |
| Order Executed with Price | `C` | Trade | Uses execution price |
| Trade (non-cross) | `P` | Trade | Direct mapping |
| Order Cancel | `X` | CancelOrder | Partial cancel |
| Order Delete | `D` | CancelOrder | Full cancel |
| Order Replace | `U` | ModifyOrder | Price and qty change |
| System Event | `S` | — | Logged, not converted |
| Stock Directory | `R` | — | Used for ticker→instrument_id mapping |
| Broken Trade | `B` | — | Skipped |
| Cross Trade | `Q` | — | Skipped |
| Trading Action | `H` | — | Logged, not converted |

### ITCH-specific handling

- **Byte order**: ITCH is big-endian; our format is little-endian. The
  converter handles the swap.
- **Price scaling**: ITCH prices are in 1/10000ths of a dollar (int32).
  We divide by 100 to get price in cents (uint32).
- **Symbol filtering**: The converter accepts `--symbol AAPL` to extract a
  single ticker. It first reads all `Stock Directory` messages to build the
  locate code → ticker mapping, then filters by locate code.
- **Order tracking**: The converter maintains an in-memory map of order IDs
  to track price/side/remaining qty for execution and cancel messages.
