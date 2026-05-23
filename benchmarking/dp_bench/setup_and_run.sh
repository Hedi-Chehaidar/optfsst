#!/usr/bin/env bash
# Build the dp_bench binaries (production OptFSST vs the no-unrolling /
# no-branch-hint variant of SymbolTable::buildDP) and run them over
# data/refined/.
#
# Output CSV: benchmarking/csv/dp_buildDP_variants.csv
#
# Required tools: cmake, ninja or make, g++, python3, taskset.

set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BENCH_DIR="$ROOT_DIR/benchmarking/dp_bench"
BUILD_DIR="$ROOT_DIR/build-dp-bench"
CSV_OUT="$ROOT_DIR/benchmarking/csv/dp_buildDP_variants.csv"
DATA_ROOT="$ROOT_DIR/data/refined"
REPS="${DP_BENCH_REPS:-5}"
PIN_CPU="${DP_BENCH_CPU:-0}"
MAX_BYTES="${DP_BENCH_MAX_BYTES:-268435456}"  # 256 MiB

step() { printf '\n==> %s\n' "$1"; }
fail() { printf 'Error: %s\n' "$1" >&2; exit 1; }
trap 'fail "setup_and_run.sh failed at line $LINENO"' ERR

for tool in cmake g++ python3 taskset; do
    command -v "$tool" >/dev/null 2>&1 || fail "required tool not found: $tool"
done

if [[ ! -d "$DATA_ROOT" ]]; then
    fail "data root not found: $DATA_ROOT (run data/refine_datasets.py first)"
fi

mkdir -p "$BUILD_DIR" "$(dirname "$CSV_OUT")"

# Only pick a generator on a fresh build dir; reuse the cached one on reruns
# so we don't fail with "generator does not match the generator used previously".
GENERATOR=""
if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]] && command -v ninja >/dev/null 2>&1; then
    GENERATOR="-G Ninja"
fi

step "Configuring dp_bench in $BUILD_DIR"
cmake -S "$BENCH_DIR" -B "$BUILD_DIR" $GENERATOR -DCMAKE_BUILD_TYPE=Release >/dev/null

step "Building dp_bench_opt and dp_bench_naive"
cmake --build "$BUILD_DIR" --target dp_bench_opt dp_bench_naive -j"$(nproc)"

step "Running dp_bench over $DATA_ROOT"
rm -f "$CSV_OUT"
python3 "$BENCH_DIR/run_bench.py" \
    --root "$DATA_ROOT" \
    --bench-opt   "$BUILD_DIR/dp_bench_opt" \
    --bench-naive "$BUILD_DIR/dp_bench_naive" \
    --out "$CSV_OUT" \
    --reps "$REPS" \
    --cpu "$PIN_CPU" \
    --max-bytes "$MAX_BYTES"

step "CSV written to $CSV_OUT"
wc -l "$CSV_OUT"
