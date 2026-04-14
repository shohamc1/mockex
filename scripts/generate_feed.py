#!/usr/bin/env python3
"""Generate synthetic binary feed files for the mini trading stack."""

import struct
import argparse
import random
import time

MSG_ADD = 0
MSG_MODIFY = 1
MSG_CANCEL = 2
MSG_TRADE = 3
MSG_CLEAR = 4


def write_msg(f, msg_type, payload):
    header = struct.pack("<BH", msg_type, len(payload))
    f.write(header)
    f.write(payload)


def make_add(seq, ts, order_id, inst_id, price, qty, side):
    return struct.pack("<QQQIIIB", seq, ts, order_id, inst_id, price, qty, side)


def make_modify(seq, ts, order_id, inst_id, new_price, new_qty):
    return struct.pack("<QQQIII", seq, ts, order_id, inst_id, new_price, new_qty)


def make_cancel(seq, ts, order_id, inst_id, canceled_qty):
    return struct.pack("<QQQII", seq, ts, order_id, inst_id, canceled_qty)


def make_trade(seq, ts, order_id, inst_id, price, qty, side):
    return struct.pack("<QQQIIIB", seq, ts, order_id, inst_id, price, qty, side)


def make_clear(seq, ts, inst_id):
    return struct.pack("<QQI", seq, ts, inst_id)


def main():
    parser = argparse.ArgumentParser(description="Generate synthetic feed data")
    parser.add_argument(
        "-o", "--output", default="data/synthetic_feed.bin", help="Output file path"
    )
    parser.add_argument(
        "-n",
        "--num-messages",
        type=int,
        default=100000,
        help="Number of messages to generate",
    )
    parser.add_argument(
        "-p", "--base-price", type=int, default=10000, help="Base price in ticks"
    )
    parser.add_argument(
        "-s", "--spread", type=int, default=10, help="Typical spread in ticks"
    )
    parser.add_argument("-i", "--instrument", type=int, default=0, help="Instrument ID")
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

    with open(args.output, "wb") as f:
        seq = 0
        ts = 1000000
        next_order_id = 1
        active_orders = []
        mid = args.base_price

        n_add = 0
        n_cancel = 0
        n_modify = 0
        n_trade = 0

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

                write_msg(
                    f,
                    MSG_ADD,
                    make_add(seq, ts, oid, args.instrument, price, qty, side),
                )
                n_add += 1

            elif action == 1:
                idx = random.randint(0, len(active_orders) - 1)
                oid, old_price, old_qty, side = active_orders[idx]
                new_price = old_price + random.randint(-5, 5)
                new_price = max(1, new_price)
                new_qty = random.choice([100, 200, 300])

                active_orders[idx] = (oid, new_price, new_qty, side)
                write_msg(
                    f,
                    MSG_MODIFY,
                    make_modify(seq, ts, oid, args.instrument, new_price, new_qty),
                )
                n_modify += 1

            elif action == 2:
                idx = random.randint(0, len(active_orders) - 1)
                oid, price, qty, side = active_orders[idx]
                if args.target_orders > 0:
                    cancel_qty = qty
                else:
                    cancel_qty = random.randint(1, qty)

                if cancel_qty >= qty:
                    active_orders.pop(idx)
                else:
                    active_orders[idx] = (oid, price, qty - cancel_qty, side)

                write_msg(
                    f,
                    MSG_CANCEL,
                    make_cancel(seq, ts, oid, args.instrument, cancel_qty),
                )
                n_cancel += 1

            else:
                idx = random.randint(0, len(active_orders) - 1)
                oid, price, qty, side = active_orders[idx]
                trade_qty = random.randint(1, qty)

                if trade_qty >= qty:
                    active_orders.pop(idx)
                else:
                    active_orders[idx] = (oid, price, qty - trade_qty, side)

                write_msg(
                    f,
                    MSG_TRADE,
                    make_trade(seq, ts, oid, args.instrument, price, trade_qty, side),
                )
                n_trade += 1

        write_msg(f, MSG_CLEAR, make_clear(seq + 1, ts + 1, args.instrument))

    if args.stats:
        print(f"Generated {args.num_messages + 1} messages -> {args.output}")
        print(f"  Add:    {n_add}")
        print(f"  Modify: {n_modify}")
        print(f"  Cancel: {n_cancel}")
        print(f"  Trade:  {n_trade}")
        print(f"  Clear:  1")
        import os

        size = os.path.getsize(args.output)
        print(f"  Size:   {size:,} bytes ({size / 1024 / 1024:.1f} MB)")


if __name__ == "__main__":
    main()
