#!/usr/bin/env bash

set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR_AVX="$ROOT_DIR/build-avx512"
BUILD_DIR_NOAVX="$ROOT_DIR/build-noavx512"
BENCH_DIR="$ROOT_DIR/benchmarking"

step() {
    printf '\n==> %s\n' "$1"
}

fail() {
    printf 'Error: %s\n' "$1" >&2
    exit 1
}

trap 'fail "run.sh failed at line $LINENO"' ERR

step "Configuring AVX build"
rm -rf "$BUILD_DIR_AVX"
cmake -S "$ROOT_DIR" -B "$BUILD_DIR_AVX"

step "Building AVX fsst"
cmake --build "$BUILD_DIR_AVX" -j

step "Configuring non-AVX build"
rm -rf "$BUILD_DIR_NOAVX"
cmake -S "$ROOT_DIR" -B "$BUILD_DIR_NOAVX" -DFSST_DISABLE_AVX512=ON

step "Building non-AVX fsst"
cmake --build "$BUILD_DIR_NOAVX" -j

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
