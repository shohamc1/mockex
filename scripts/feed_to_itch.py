#!/usr/bin/env python3
"""Convert mockex binary feed format to NASDAQ TotalView ITCH 5.0 format.

This allows running CppTrader benchmarks on the same data as mockex.

Wire format (mockex): [MsgType:1][PayloadLen:2][Payload:PayloadLen]
  - All fields little-endian
  - Header: 3 bytes

ITCH 5.0 format: [Length:2][Payload:Length]
  - All fields big-endian
  - Payload[0] = message type character
"""

import struct
import argparse
import os
import sys

MSG_ADD = 0
MSG_MODIFY = 1
MSG_CANCEL = 2
MSG_TRADE = 3
MSG_CLEAR = 4

ITCH_STOCK_LOCATE = 1
ITCH_TRACKING_NUM = 1
ITCH_STOCK = b"MOCKEX  "


def write_itch_msg(out, payload):
    out.write(struct.pack(">H", len(payload)))
    out.write(payload)


def itch_timestamp(ns):
    return ns % 86400000000000


def make_system_event(ts, event_code=b"O"):
    return b"S" + struct.pack(">HQQ", 0, 0, itch_timestamp(ts)) + event_code + b"\x00"


def make_stock_directory(ts, locate):
    payload = b"R"
    payload += struct.pack(">H", locate)
    payload += struct.pack(">Q", itch_timestamp(ts))
    payload += ITCH_STOCK
    payload += b"\x00" * 4
    payload += b"\x00" * 2
    payload += b"\x00"
    payload += struct.pack(">I", 10000)
    payload += b"\x00" * 3
    payload += b"\x00" * 5
    payload += b"\x00"
    payload += b"\x00" * 2
    payload += b"\x00"
    return payload


def make_add_order(ts, order_id, shares, price, side_char):
    payload = b"A"
    payload += struct.pack(">H", ITCH_STOCK_LOCATE)
    payload += struct.pack(">H", ITCH_TRACKING_NUM)
    payload += struct.pack(">Q", itch_timestamp(ts))
    payload += struct.pack(">Q", order_id)
    payload += side_char
    payload += struct.pack(">I", shares)
    payload += struct.pack(">I", price * 100)
    payload += b"\x00" * 4
    return payload


def make_order_execute(ts, order_id, executed_shares, match_number):
    payload = b"E"
    payload += struct.pack(">H", ITCH_STOCK_LOCATE)
    payload += struct.pack(">H", ITCH_TRACKING_NUM)
    payload += struct.pack(">Q", itch_timestamp(ts))
    payload += struct.pack(">Q", order_id)
    payload += struct.pack(">I", executed_shares)
    payload += struct.pack(">Q", match_number)
    return payload


def make_order_cancel(ts, order_id, canceled_shares):
    payload = b"X"
    payload += struct.pack(">H", ITCH_STOCK_LOCATE)
    payload += struct.pack(">H", ITCH_TRACKING_NUM)
    payload += struct.pack(">Q", itch_timestamp(ts))
    payload += struct.pack(">Q", order_id)
    payload += struct.pack(">I", canceled_shares)
    return payload


def make_order_delete(ts, order_id):
    payload = b"D"
    payload += struct.pack(">H", ITCH_STOCK_LOCATE)
    payload += struct.pack(">H", ITCH_TRACKING_NUM)
    payload += struct.pack(">Q", itch_timestamp(ts))
    payload += struct.pack(">Q", order_id)
    return payload


def make_order_replace(ts, orig_order_id, new_order_id, shares, price):
    payload = b"U"
    payload += struct.pack(">H", ITCH_STOCK_LOCATE)
    payload += struct.pack(">H", ITCH_TRACKING_NUM)
    payload += struct.pack(">Q", itch_timestamp(ts))
    payload += struct.pack(">Q", orig_order_id)
    payload += struct.pack(">Q", new_order_id)
    payload += struct.pack(">I", shares)
    payload += struct.pack(">I", price * 100)
    return payload


def read_u64_le(data, off):
    return struct.unpack_from("<Q", data, off)[0]


def read_u32_le(data, off):
    return struct.unpack_from("<I", data, off)[0]


def read_u8_le(data, off):
    return data[off]


def main():
    parser = argparse.ArgumentParser(
        description="Convert mockex binary feed to NASDAQ ITCH 5.0 format"
    )
    parser.add_argument("-i", "--input", required=True, help="Input mockex feed file")
    parser.add_argument("-o", "--output", required=True, help="Output ITCH 5.0 file")
    parser.add_argument(
        "--stats", action="store_true", help="Print conversion statistics"
    )
    args = parser.parse_args()

    if not os.path.exists(args.input):
        print(f"Error: {args.input} not found", file=sys.stderr)
        sys.exit(1)

    total_size = os.path.getsize(args.input)
    orders = {}
    next_new_id = 1_000_000_000
    match_number = 1
    counts = {
        "add": 0,
        "execute": 0,
        "cancel_partial": 0,
        "delete": 0,
        "replace": 0,
        "clear": 0,
        "total_itch": 0,
    }
    preamble_done = False

    with open(args.input, "rb") as fin, open(args.output, "wb") as fout:
        wrote_preamble = False
        pos = 0

        while pos + 3 <= total_size:
            fin.seek(pos)
            hdr = fin.read(3)
            if len(hdr) < 3:
                break

            msg_type = hdr[0]
            payload_len = struct.unpack_from("<H", hdr, 1)[0]
            total_msg = 3 + payload_len

            if pos + total_msg > total_size:
                break

            data = fin.read(payload_len)
            if len(data) < payload_len:
                break
            pos += total_msg

            ts = read_u64_le(data, 8) if payload_len >= 16 else 0

            if not wrote_preamble:
                write_itch_msg(fout, make_system_event(ts, b"O"))
                counts["total_itch"] += 1
                write_itch_msg(fout, make_stock_directory(ts, ITCH_STOCK_LOCATE))
                counts["total_itch"] += 1
                wrote_preamble = True

            if msg_type == MSG_ADD:
                order_id = read_u64_le(data, 16)
                price = read_u32_le(data, 28)
                qty = read_u32_le(data, 32)
                side = read_u8_le(data, 36)
                side_char = b"B" if side == 0 else b"S"
                orders[order_id] = {"price": price, "qty": qty, "side": side}
                write_itch_msg(
                    fout, make_add_order(ts, order_id, qty, price, side_char)
                )
                counts["add"] += 1
                counts["total_itch"] += 1

            elif msg_type == MSG_TRADE:
                order_id = read_u64_le(data, 16)
                trade_qty = read_u32_le(data, 32)
                if order_id in orders:
                    orders[order_id]["qty"] -= trade_qty
                    if orders[order_id]["qty"] <= 0:
                        del orders[order_id]
                write_itch_msg(
                    fout,
                    make_order_execute(ts, order_id, trade_qty, match_number),
                )
                match_number += 1
                counts["execute"] += 1
                counts["total_itch"] += 1

            elif msg_type == MSG_CANCEL:
                order_id = read_u64_le(data, 16)
                cancel_qty = read_u32_le(data, 28)
                if order_id in orders:
                    remaining = orders[order_id]["qty"] - cancel_qty
                    if remaining > 0:
                        orders[order_id]["qty"] = remaining
                        write_itch_msg(
                            fout,
                            make_order_cancel(ts, order_id, cancel_qty),
                        )
                        counts["cancel_partial"] += 1
                    else:
                        write_itch_msg(fout, make_order_delete(ts, order_id))
                        counts["delete"] += 1
                        del orders[order_id]
                else:
                    write_itch_msg(fout, make_order_delete(ts, order_id))
                    counts["delete"] += 1
                counts["total_itch"] += 1

            elif msg_type == MSG_MODIFY:
                order_id = read_u64_le(data, 16)
                new_price = read_u32_le(data, 28)
                new_qty = read_u32_le(data, 32)
                new_order_id = next_new_id
                next_new_id += 1
                if order_id in orders:
                    del orders[order_id]
                orders[new_order_id] = {
                    "price": new_price,
                    "qty": new_qty,
                    "side": 0,
                }
                write_itch_msg(
                    fout,
                    make_order_replace(ts, order_id, new_order_id, new_qty, new_price),
                )
                counts["replace"] += 1
                counts["total_itch"] += 1

            elif msg_type == MSG_CLEAR:
                counts["clear"] += 1

    if args.stats:
        out_size = os.path.getsize(args.output)
        print(f"Conversion complete: {args.output}")
        print(f"  Input:  {args.input} ({total_size:,} bytes)")
        print(f"  Output: {args.output} ({out_size:,} bytes)")
        print(f"  ITCH messages written: {counts['total_itch']}")
        print(f"    AddOrder (A):      {counts['add']}")
        print(f"    Execute (E):       {counts['execute']}")
        print(f"    PartialCancel (X): {counts['cancel_partial']}")
        print(f"    Delete (D):        {counts['delete']}")
        print(f"    Replace (U):       {counts['replace']}")
        print(f"    Skipped (clear):   {counts['clear']}")


if __name__ == "__main__":
    main()
