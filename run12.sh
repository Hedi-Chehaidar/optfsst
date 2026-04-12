#!/usr/bin/env bash

set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT_DIR/build12"
BENCH_DIR="$ROOT_DIR/benchmarking"

step() {
    printf '\n==> %s\n' "$1"
}

fail() {
    printf 'Error: %s\n' "$1" >&2
    exit 1
}

trap 'fail "run12.sh failed at line $LINENO"' ERR

step "Configuring 12-bit build"
rm -rf "$BUILD_DIR"
cmake -S "$ROOT_DIR" -B "$BUILD_DIR"

step "Building fsst12 executable"
cmake --build "$BUILD_DIR" -j --target binary12

step "Building 12-bit benchmark runner"
g++ "$BENCH_DIR/runner12.cpp" -o "$BENCH_DIR/runner12"

step "Running 12-bit benchmarks"
(
    cd "$BENCH_DIR"
    ./runner12
)

if ! python3 -c "import pandas" >/dev/null 2>&1; then
    fail "python module 'pandas' is not installed; skipping plot generation"
fi

step "Generating 12-bit plots"
(
    cd "$BENCH_DIR"
    python3 plot_results.py compression_speed_dbtext12
    python3 plot_results.py decompression_speed_dbtext12
)
