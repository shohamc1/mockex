#!/usr/bin/env python3
"""Generate synthetic NASDAQ TotalView ITCH 5.0 binary feed files.

Mirrors the order-flow logic of generate_feed.py (same seed produces same
order actions), but serializes as proper ITCH 5.0 binary format per the
official NASDAQ specification (big-endian, 2-byte length prefix, 6-byte
timestamps, Price(4) in 1/10000ths of a dollar).
"""

import struct
import argparse
import random
import os


def write_ts6(ts_ns):
    return struct.pack(">Q", ts_ns)[2:]


def write_msg(f, payload):
    f.write(struct.pack(">H", len(payload)))
    f.write(payload)


def make_system_event(ts_ns, event_code):
    return (
        b"S" + struct.pack(">HH", 0, 0) + write_ts6(ts_ns) + event_code.encode("ascii")
    )


def make_stock_directory(locate, ts_ns, symbol):
    payload = bytearray(39)
    payload[0] = ord("R")
    struct.pack_into(">H", payload, 1, locate)
    struct.pack_into(">H", payload, 3, 0)
    payload[5:11] = write_ts6(ts_ns)
    payload[11:19] = symbol.ljust(8).encode("ascii")
    payload[19] = ord("Q")
    payload[20] = ord("N")
    struct.pack_into(">I", payload, 21, 100)
    payload[25] = ord("N")
    payload[26] = ord("C")
    payload[27:29] = b"  "
    payload[29] = ord("P")
    payload[30] = ord(" ")
    payload[31] = ord("N")
    payload[32] = ord("1")
    payload[33] = ord("N")
    struct.pack_into(">I", payload, 34, 0)
    payload[38] = ord("N")
    return bytes(payload)


def make_add_order(locate, ts_ns, order_id, side, qty, symbol, price_raw):
    payload = bytearray(36)
    payload[0] = ord("A")
    struct.pack_into(">H", payload, 1, locate)
    struct.pack_into(">H", payload, 3, 0)
    payload[5:11] = write_ts6(ts_ns)
    struct.pack_into(">Q", payload, 11, order_id)
    payload[19] = ord(side)
    struct.pack_into(">I", payload, 20, qty)
    payload[24:32] = symbol.ljust(8).encode("ascii")
    struct.pack_into(">i", payload, 32, price_raw)
    return bytes(payload)


def make_order_executed(locate, ts_ns, order_id, exec_qty, match_number):
    payload = bytearray(31)
    payload[0] = ord("E")
    struct.pack_into(">H", payload, 1, locate)
    struct.pack_into(">H", payload, 3, 0)
    payload[5:11] = write_ts6(ts_ns)
    struct.pack_into(">Q", payload, 11, order_id)
    struct.pack_into(">I", payload, 19, exec_qty)
    struct.pack_into(">Q", payload, 23, match_number)
    return bytes(payload)


def make_order_cancel(locate, ts_ns, order_id, canceled_shares):
    payload = bytearray(24)
    payload[0] = ord("X")
    struct.pack_into(">H", payload, 1, locate)
    struct.pack_into(">H", payload, 3, 0)
    payload[5:11] = write_ts6(ts_ns)
    struct.pack_into(">Q", payload, 11, order_id)
    struct.pack_into(">I", payload, 19, canceled_shares)
    return bytes(payload)


def make_order_delete(locate, ts_ns, order_id):
    payload = bytearray(19)
    payload[0] = ord("D")
    struct.pack_into(">H", payload, 1, locate)
    struct.pack_into(">H", payload, 3, 0)
    payload[5:11] = write_ts6(ts_ns)
    struct.pack_into(">Q", payload, 11, order_id)
    return bytes(payload)


def make_order_replace(locate, ts_ns, orig_order_id, new_order_id, shares, price_raw):
    payload = bytearray(35)
    payload[0] = ord("U")
    struct.pack_into(">H", payload, 1, locate)
    struct.pack_into(">H", payload, 3, 0)
    payload[5:11] = write_ts6(ts_ns)
    struct.pack_into(">Q", payload, 11, orig_order_id)
    struct.pack_into(">Q", payload, 19, new_order_id)
    struct.pack_into(">I", payload, 27, shares)
    struct.pack_into(">i", payload, 31, price_raw)
    return bytes(payload)


def main():
    parser = argparse.ArgumentParser(
        description="Generate synthetic ITCH 5.0 binary feed data"
    )
    parser.add_argument(
        "-o", "--output", default="data/synthetic_itch.bin", help="Output file path"
    )
    parser.add_argument(
        "-n",
        "--num-messages",
        type=int,
        default=100000,
        help="Number of order messages to generate (excluding admin messages)",
    )
    parser.add_argument(
        "-p", "--base-price", type=int, default=10000, help="Base price in ticks"
    )
    parser.add_argument(
        "-s", "--spread", type=int, default=10, help="Typical spread in ticks"
    )
    parser.add_argument("--symbol", default="MOCKEX", help="Stock symbol (8 chars max)")
    parser.add_argument("--locate", type=int, default=1, help="Stock locate code")
    parser.add_argument(
        "--seed", type=int, default=42, help="Random seed for determinism"
    )
    parser.add_argument("--stats", action="store_true", help="Print statistics")
    parser.add_argument(
        "--target-orders",
        type=int,
        default=0,
        help="Target active order count (0=unbounded old behavior)",
    )
    args = parser.parse_args()

    random.seed(args.seed)

    locate = args.locate
    symbol = args.symbol[:8]
    price_scale = 100

    with open(args.output, "wb") as f:
        seq = 0
        ts = 1000000
        next_order_id = 1
        next_match_number = 1
        active_orders = []
        mid = args.base_price

        n_add = 0
        n_cancel_partial = 0
        n_cancel_full = 0
        n_replace = 0
        n_trade = 0

        ts_admin = ts - 1
        write_msg(f, make_system_event(ts_admin, "O"))
        write_msg(f, make_stock_directory(locate, ts_admin, symbol))
        write_msg(f, make_system_event(ts_admin + 1, "S"))

        for _ in range(args.num_messages):
            seq += 1
            ts += random.randint(1, 100)
            mid += random.choice([-1, -1, 0, 0, 0, 1, 1])
            mid = max(args.base_price - 500, min(args.base_price + 500, mid))

            if not active_orders:
                r = 0.0
            else:
                r = random.random()

            if args.target_orders > 0 and active_orders:
                frac = len(active_orders) / args.target_orders
                add_p = max(0.05, 0.35 - 0.30 * min(frac, 1.5))
                cancel_p = max(0.10, 0.35 + 0.30 * min(frac, 1.5))
                modify_p = 0.20
                trade_p = max(0.05, 1.0 - add_p - cancel_p - modify_p)
                action = 0
                if r >= add_p:
                    action = 1
                if r >= add_p + modify_p:
                    action = 2
                if r >= add_p + modify_p + cancel_p:
                    action = 3
            else:
                if r < 0.6 or not active_orders:
                    action = 0
                elif r < 0.8:
                    action = 1
                elif r < 0.95:
                    action = 2
                else:
                    action = 3

            if action == 0 or not active_orders:
                side = random.choice([0, 1])
                offset = random.randint(0, args.spread * 3)
                if side == 0:
                    price = mid - offset
                else:
                    price = mid + offset
                price = max(1, price)
                qty = random.choice([100, 100, 200, 300, 500])

                oid = next_order_id
                next_order_id += 1
                active_orders.append((oid, price, qty, side))

                side_char = "B" if side == 0 else "S"
                price_raw = price * price_scale
                write_msg(
                    f,
                    make_add_order(locate, ts, oid, side_char, qty, symbol, price_raw),
                )
                n_add += 1

            elif action == 1:
                idx = random.randint(0, len(active_orders) - 1)
                oid, old_price, old_qty, side = active_orders[idx]
                new_price = old_price + random.randint(-5, 5)
                new_price = max(1, new_price)
                new_qty = random.choice([100, 200, 300])

                new_oid = next_order_id
                next_order_id += 1
                active_orders[idx] = (new_oid, new_price, new_qty, side)

                price_raw = new_price * price_scale
                write_msg(
                    f, make_order_replace(locate, ts, oid, new_oid, new_qty, price_raw)
                )
                n_replace += 1

            elif action == 2:
                idx = random.randint(0, len(active_orders) - 1)
                oid, price, qty, side = active_orders[idx]
                if args.target_orders > 0:
                    cancel_qty = qty
                else:
                    cancel_qty = random.randint(1, qty)

                if cancel_qty >= qty:
                    active_orders.pop(idx)
                    write_msg(f, make_order_delete(locate, ts, oid))
                    n_cancel_full += 1
                else:
                    active_orders[idx] = (oid, price, qty - cancel_qty, side)
                    write_msg(f, make_order_cancel(locate, ts, oid, cancel_qty))
                    n_cancel_partial += 1

            else:
                idx = random.randint(0, len(active_orders) - 1)
                oid, price, qty, side = active_orders[idx]
                trade_qty = random.randint(1, qty)

                match_num = next_match_number
                next_match_number += 1

                if trade_qty >= qty:
                    active_orders.pop(idx)
                else:
                    active_orders[idx] = (oid, price, qty - trade_qty, side)

                write_msg(f, make_order_executed(locate, ts, oid, trade_qty, match_num))
                n_trade += 1

        ts_end = ts + 1
        write_msg(f, make_system_event(ts_end, "C"))

    if args.stats:
        n_total = n_add + n_cancel_partial + n_cancel_full + n_replace + n_trade
        print(f"Generated {n_total} order messages + admin messages -> {args.output}")
        print(f"  Add Order (A):       {n_add}")
        print(f"  Order Replace (U):   {n_replace}")
        print(f"  Order Cancel (X):    {n_cancel_partial}")
        print(f"  Order Delete (D):    {n_cancel_full}")
        print(f"  Order Executed (E):  {n_trade}")
        print(f"  System Event (S):    3")
        print(f"  Stock Directory (R): 1")
        size = os.path.getsize(args.output)
        print(f"  Size:   {size:,} bytes ({size / 1024 / 1024:.1f} MB)")


if __name__ == "__main__":
    main()
