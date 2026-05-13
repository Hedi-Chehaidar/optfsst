#!/usr/bin/env bash

set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR_AVX="$ROOT_DIR/build-avx512"
BUILD_DIR_NOAVX="$ROOT_DIR/build-noavx512"
BUILD_DIR_12="$ROOT_DIR/build12"
BENCH_DIR="$ROOT_DIR/benchmarking"

step() {
    printf '\n==> %s\n' "$1"
}

fail() {
    printf 'Error: %s\n' "$1" >&2
    exit 1
}

trap 'fail "run.sh failed at line $LINENO"' ERR

step "Configuring AVX-512 FSST build"
rm -rf "$BUILD_DIR_AVX"
cmake -S "$ROOT_DIR" -B "$BUILD_DIR_AVX"

step "Building AVX-512 FSST binary"
cmake --build "$BUILD_DIR_AVX" -j --target binary

step "Configuring scalar FSST build"
rm -rf "$BUILD_DIR_NOAVX"
cmake -S "$ROOT_DIR" -B "$BUILD_DIR_NOAVX" -DFSST_DISABLE_AVX512=ON

step "Building scalar FSST binary"
cmake --build "$BUILD_DIR_NOAVX" -j --target binary

step "Configuring FSST12 build"
rm -rf "$BUILD_DIR_12"
cmake -S "$ROOT_DIR" -B "$BUILD_DIR_12"

step "Building FSST12 binary"
cmake --build "$BUILD_DIR_12" -j --target binary12

step "Building benchmark runner"
g++ -std=c++20 -O3 "$BENCH_DIR/runner.cpp" -o "$BENCH_DIR/runner"

step "Building Snappy benchmark helper (optional)"
rm -f "$BENCH_DIR/snappy_bench"
if g++ -std=c++20 -O3 "$BENCH_DIR/snappy_bench.cpp" -o "$BENCH_DIR/snappy_bench" -lsnappy 2>/dev/null; then
    echo "built snappy_bench"
else
    echo "warning: libsnappy not available (install libsnappy-dev), skipping Snappy in cf_block_compressors benchmark" >&2
fi

step "Running paper benchmarks"
(
    cd "$BENCH_DIR"
    ./runner
)

if ! python3 -c "import pandas, matplotlib, seaborn" >/dev/null 2>&1; then
    fail "python plotting dependencies are not installed"
fi

step "Generating plots"
(
    cd "$BENCH_DIR"
    export MPLCONFIGDIR="/tmp/optfsst-mplconfig"
    mkdir -p "$MPLCONFIGDIR"
    python3 plot_results.py improvement
    python3 plot_results.py improvement12
    python3 plot_results.py compression_speed_paper
    python3 plot_results.py decompression_speed_paper
    python3 plot_results.py table_construction_speed_paper
    if [[ -f "./csv/cf_block_compressors.csv" ]]; then
        python3 plot_results.py cf_block_compressors
        python3 plot_results.py cf_block_compressors_table
    else
        echo "warning: csv/cf_block_compressors.csv not found, skipping cf_block_compressors plot" >&2
    fi
)
