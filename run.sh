#!/usr/bin/env bash

set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT_DIR/build"
BENCH_DIR="$ROOT_DIR/benchmarking"

step() {
    printf '\n==> %s\n' "$1"
}

fail() {
    printf 'Error: %s\n' "$1" >&2
    exit 1
}

trap 'fail "run.sh failed at line $LINENO"' ERR

step "Configuring build"
rm -rf "$BUILD_DIR"
cmake -S "$ROOT_DIR" -B "$BUILD_DIR"

step "Building fsst"
cmake --build "$BUILD_DIR" -j

step "Building benchmark runner"
g++ "$BENCH_DIR/runner.cpp" -o "$BENCH_DIR/runner"

step "Running benchmarks"
(
    cd "$BENCH_DIR"
    ./runner
)

if ! python3 -c "import pandas" >/dev/null 2>&1; then
    fail "python module 'pandas' is not installed; skipping plot generation"
fi

step "Generating plots"
(
    cd "$BENCH_DIR"
    python3 plot_results.py compression_speed_dbtext
    python3 plot_results.py decompression_speed_dbtext
)
