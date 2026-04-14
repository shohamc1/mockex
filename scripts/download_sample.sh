#!/bin/bash
# Download the latest NASDAQ TotalView ITCH 5.0 sample file.
# Free one-day samples published by NASDAQ at emi.nasdaq.com.

set -e

DATA_DIR="$(cd "$(dirname "$0")/.." && pwd)/data"
mkdir -p "$DATA_DIR"

ITCH_URL="https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH/12302019.NASDAQ_ITCH50.gz"
BASENAME="12302019.NASDAQ_ITCH50.bin"
GZ_NAME="${BASENAME}.gz"

echo "Downloading NASDAQ ITCH sample (Dec 30, 2019 - latest available)..."
echo "This file is ~3.5GB compressed, ~5GB uncompressed."
echo ""

if [ ! -f "$DATA_DIR/$BASENAME" ]; then
    if [ ! -f "$DATA_DIR/$GZ_NAME" ]; then
        echo "Downloading $GZ_NAME ..."
        curl -L --progress-bar -o "$DATA_DIR/$GZ_NAME" "$ITCH_URL"
    fi
    echo "Decompressing..."
    gunzip -k "$DATA_DIR/$GZ_NAME"
    echo "Done: $DATA_DIR/$BASENAME"
else
    echo "File already exists: $DATA_DIR/$BASENAME"
fi

echo ""
echo "To convert to internal format, run:"
echo "  python3 scripts/itch_to_feed.py -i data/$BASENAME -o data/aapl_feed.bin -s AAPL -n 500000 --stats"
