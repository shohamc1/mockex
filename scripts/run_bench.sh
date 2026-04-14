#!/bin/bash
set -e

BUILD_DIR="build"

echo "=== Building mini-trading-stack ==="
cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release .
cmake --build "$BUILD_DIR" -j$(sysctl -n hw.ncpu 2>/dev/null || echo 4)

echo ""
echo "=== Generating synthetic feed ==="
python3 scripts/generate_feed.py -o data/synthetic_feed.bin -n 500000 --stats

echo ""
echo "=== Running end-to-end replay ==="
./build/mini_trader data/synthetic_feed.bin --verbose --max-msgs 1000

echo ""
echo "=== Running benchmark (5 iterations) ==="
./build/mini_trader_bench data/synthetic_feed.bin 5
