#!/usr/bin/env python3
"""Convert NASDAQ TotalView ITCH 5.0 binary file to internal feed format.

Field offsets per the official NASDAQ ITCH 5.0 specification.
All ITCH fields are big-endian. Timestamps are 6 bytes (nanoseconds since midnight).
Prices are int32 in 1/10000ths of a dollar (Price(4)).
"""

import struct
import argparse
import os
import sys
from collections import defaultdict

MSG_ADD = 0
MSG_MODIFY = 1
MSG_CANCEL = 2
MSG_TRADE = 3
MSG_CLEAR = 4


def read_msg(f):
    length_data = f.read(2)
    if len(length_data) < 2:
        return None
    length = struct.unpack(">H", length_data)[0]
    payload = f.read(length)
    if len(payload) < length:
        return None
    msg_type = chr(payload[0])
    return msg_type, payload


def parse_uint16(data, offset):
    return struct.unpack(">H", data[offset : offset + 2])[0]


def parse_uint32(data, offset):
    return struct.unpack(">I", data[offset : offset + 4])[0]


def parse_int32(data, offset):
    return struct.unpack(">i", data[offset : offset + 4])[0]


def parse_ts6(data, offset):
    return struct.unpack(">Q", b"\x00\x00" + data[offset : offset + 6])[0]


def parse_uint64(data, offset):
    return struct.unpack(">Q", data[offset : offset + 8])[0]


def parse_str(data, offset, length):
    return data[offset : offset + length].decode("ascii", errors="replace").strip()


def write_msg(out, msg_type, payload):
    header = struct.pack("<BH", msg_type, len(payload))
    out.write(header)
    out.write(payload)


def make_add(seq, ts, order_id, inst_id, price_ticks, qty, side):
    return struct.pack("<QQQIIIB", seq, ts, order_id, inst_id, price_ticks, qty, side)


def make_cancel(seq, ts, order_id, inst_id, canceled_qty):
    return struct.pack("<QQQII", seq, ts, order_id, inst_id, canceled_qty)


def make_trade(seq, ts, order_id, inst_id, price_ticks, qty, side):
    return struct.pack("<QQQIIIB", seq, ts, order_id, inst_id, price_ticks, qty, side)


def make_modify(seq, ts, order_id, inst_id, new_price, new_qty):
    return struct.pack("<QQQIII", seq, ts, order_id, inst_id, new_price, new_qty)


def make_clear(seq, ts, inst_id):
    return struct.pack("<QQI", seq, ts, inst_id)


def main():
    parser = argparse.ArgumentParser(
        description="Convert NASDAQ ITCH 5.0 to internal binary feed format"
    )
    parser.add_argument(
        "-i", "--input", required=True, help="Input ITCH 5.0 binary file"
    )
    parser.add_argument(
        "-o",
        "--output",
        default=None,
        help="Output feed file (default: derived from input)",
    )
    parser.add_argument(
        "-s", "--symbol", default=None, help="Filter to a single stock symbol"
    )
    parser.add_argument(
        "-n",
        "--max-messages",
        type=int,
        default=0,
        help="Max output messages (0 = unlimited)",
    )
    parser.add_argument(
        "--stats", action="store_true", help="Print conversion statistics"
    )
    args = parser.parse_args()

    if not os.path.exists(args.input):
        print(f"Error: input file not found: {args.input}", file=sys.stderr)
        sys.exit(1)

    if args.output is None:
        base = (
            os.path.splitext(args.path)[0]
            if hasattr(args, "path")
            else "data/itch_feed"
        )
        sym_suffix = f"_{args.symbol}" if args.symbol else ""
        args.output = f"{base}{sym_suffix}.bin"

    stock_map = {}
    next_inst_id = 0
    target_locate = None

    counts = defaultdict(int)
    seq = 0
    itched_orders = {}

    total_size = os.path.getsize(args.input)

    with open(args.input, "rb") as fin, open(args.output, "wb") as fout:
        pct_markers = set(range(0, 101, 10))

        while True:
            result = read_msg(fin)
            if result is None:
                break
            msg_type, payload = result
            counts[msg_type] += 1

            pos = fin.tell()
            pct = int(pos * 100 / total_size) if total_size > 0 else 0
            if pct in pct_markers:
                print(f"  {pos}/{total_size} ({pct}%)...", file=sys.stderr)
                pct_markers.discard(pct)

            if msg_type == "R":
                if len(payload) < 39:
                    continue
                loc = parse_uint16(payload, 1)
                ticker = parse_str(payload, 11, 8)
                if ticker not in stock_map:
                    stock_map[ticker] = next_inst_id
                    next_inst_id += 1
                if args.symbol and ticker == args.symbol:
                    target_locate = loc

            elif msg_type == "S":
                pass

            elif msg_type == "A":
                if len(payload) < 36:
                    continue
                locate = parse_uint16(payload, 1)
                if args.symbol and locate != target_locate:
                    continue

                ts = parse_ts6(payload, 5)
                order_id = parse_uint64(payload, 11)
                side_char = chr(payload[19])
                qty = parse_uint32(payload, 20)
                price_raw = parse_int32(payload, 32)
                price_ticks = price_raw // 100

                inst_id = 0
                side = 0 if side_char == "B" else 1

                if price_ticks == 0 or qty == 0:
                    continue

                seq += 1
                itched_orders[order_id] = (price_ticks, qty, side)
                write_msg(
                    fout,
                    MSG_ADD,
                    make_add(seq, ts, order_id, inst_id, price_ticks, qty, side),
                )
                counts["out_add"] += 1

            elif msg_type == "F":
                if len(payload) < 40:
                    continue
                locate = parse_uint16(payload, 1)
                if args.symbol and locate != target_locate:
                    continue

                ts = parse_ts6(payload, 5)
                order_id = parse_uint64(payload, 11)
                side_char = chr(payload[19])
                qty = parse_uint32(payload, 20)
                price_raw = parse_int32(payload, 32)
                price_ticks = price_raw // 100

                side = 0 if side_char == "B" else 1
                if price_ticks == 0 or qty == 0:
                    continue

                seq += 1
                itched_orders[order_id] = (price_ticks, qty, side)
                write_msg(
                    fout,
                    MSG_ADD,
                    make_add(seq, ts, order_id, 0, price_ticks, qty, side),
                )
                counts["out_add"] += 1

            elif msg_type == "E":
                if len(payload) < 31:
                    continue
                locate = parse_uint16(payload, 1)
                if args.symbol and locate != target_locate:
                    continue

                ts = parse_ts6(payload, 5)
                order_id = parse_uint64(payload, 11)
                exec_qty = parse_uint32(payload, 19)

                if order_id in itched_orders:
                    px, remaining, side = itched_orders[order_id]
                    seq += 1
                    write_msg(
                        fout,
                        MSG_TRADE,
                        make_trade(seq, ts, order_id, 0, px, exec_qty, side),
                    )
                    counts["out_trade"] += 1

                    if exec_qty >= remaining:
                        del itched_orders[order_id]
                    else:
                        itched_orders[order_id] = (px, remaining - exec_qty, side)

            elif msg_type == "C":
                if len(payload) < 36:
                    continue
                locate = parse_uint16(payload, 1)
                if args.symbol and locate != target_locate:
                    continue

                ts = parse_ts6(payload, 5)
                order_id = parse_uint64(payload, 11)
                exec_qty = parse_uint32(payload, 19)
                price_raw = parse_int32(payload, 32)
                price_ticks = price_raw // 100

                side = 0
                if order_id in itched_orders:
                    _, _, side = itched_orders[order_id]

                seq += 1
                write_msg(
                    fout,
                    MSG_TRADE,
                    make_trade(seq, ts, order_id, 0, price_ticks, exec_qty, side),
                )
                counts["out_trade"] += 1

                if order_id in itched_orders:
                    px, remaining, s = itched_orders[order_id]
                    if exec_qty >= remaining:
                        del itched_orders[order_id]
                    else:
                        itched_orders[order_id] = (px, remaining - exec_qty, s)

            elif msg_type == "X":
                if len(payload) < 24:
                    continue
                locate = parse_uint16(payload, 1)
                if args.symbol and locate != target_locate:
                    continue

                ts = parse_ts6(payload, 5)
                order_id = parse_uint64(payload, 11)
                cancel_qty = parse_uint32(payload, 19)

                if order_id in itched_orders:
                    px, remaining, side = itched_orders[order_id]
                    seq += 1
                    write_msg(
                        fout, MSG_CANCEL, make_cancel(seq, ts, order_id, 0, cancel_qty)
                    )
                    counts["out_cancel"] += 1

                    if cancel_qty >= remaining:
                        del itched_orders[order_id]
                    else:
                        itched_orders[order_id] = (px, remaining - cancel_qty, side)

            elif msg_type == "D":
                if len(payload) < 19:
                    continue
                locate = parse_uint16(payload, 1)
                if args.symbol and locate != target_locate:
                    continue

                ts = parse_ts6(payload, 5)
                order_id = parse_uint64(payload, 11)

                if order_id in itched_orders:
                    px, remaining, side = itched_orders[order_id]
                    seq += 1
                    write_msg(
                        fout, MSG_CANCEL, make_cancel(seq, ts, order_id, 0, remaining)
                    )
                    counts["out_cancel"] += 1
                    del itched_orders[order_id]

            elif msg_type == "U":
                if len(payload) < 35:
                    continue
                locate = parse_uint16(payload, 1)
                if args.symbol and locate != target_locate:
                    continue

                ts = parse_ts6(payload, 5)
                orig_order_id = parse_uint64(payload, 11)
                new_order_id = parse_uint64(payload, 19)
                new_qty = parse_uint32(payload, 27)
                new_price_raw = parse_int32(payload, 31)
                new_price = new_price_raw // 100

                if orig_order_id in itched_orders:
                    _, _, side = itched_orders[orig_order_id]
                    seq += 1
                    write_msg(
                        fout,
                        MSG_MODIFY,
                        make_modify(seq, ts, orig_order_id, 0, new_price, new_qty),
                    )
                    counts["out_modify"] += 1
                    del itched_orders[orig_order_id]
                    itched_orders[new_order_id] = (new_price, new_qty, side)

            elif msg_type == "P":
                if len(payload) < 44:
                    continue
                locate = parse_uint16(payload, 1)
                if args.symbol and locate != target_locate:
                    continue

                ts = parse_ts6(payload, 5)
                order_id = parse_uint64(payload, 11)
                side_char = chr(payload[19])
                qty = parse_uint32(payload, 20)
                price_raw = parse_int32(payload, 32)
                price_ticks = price_raw // 100

                side = 0 if side_char == "B" else 1
                if qty > 0 and price_ticks > 0:
                    seq += 1
                    write_msg(
                        fout,
                        MSG_TRADE,
                        make_trade(seq, ts, order_id, 0, price_ticks, qty, side),
                    )
                    counts["out_trade"] += 1

            if args.max_messages > 0 and seq >= args.max_messages:
                break

    if args.stats:
        out_size = os.path.getsize(args.output)
        print(f"\nConversion complete: {args.output}")
        print(f"  Input:  {args.input} ({total_size:,} bytes)")
        print(f"  Output: {args.output} ({out_size:,} bytes)")
        print(f"  Output messages: {seq}")
        print(f"  Out Add:    {counts.get('out_add', 0)}")
        print(f"  Out Modify: {counts.get('out_modify', 0)}")
        print(f"  Out Cancel: {counts.get('out_cancel', 0)}")
        print(f"  Out Trade:  {counts.get('out_trade', 0)}")
        print(f"\n  ITCH message breakdown:")
        for k, v in sorted(counts.items()):
            if not k.startswith("out_"):
                print(f"    '{k}': {v}")


if __name__ == "__main__":
    main()
