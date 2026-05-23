#!/usr/bin/env bash
# Set up FSST+ (vanilla and OptFSST+ variants) on top of this OptFSST checkout
# and run the FSST+ vs OptFSST+ benchmark over data/refined.
#
# Output CSV:
#   benchmarking/csv/cf_speed_fsst_plus_vs_optfsst_plus.csv
#
# Required tools: git, curl, unzip, cmake, ninja, g++, python3.

set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BENCH_DIR="$ROOT_DIR/benchmarking/fsst_plus_bench"
WORK_DIR="$ROOT_DIR/build-fsst-plus-bench"
DUCKDB_VERSION="v1.4.3"
DUCKDB_ZIP_URL="https://github.com/duckdb/duckdb/releases/download/${DUCKDB_VERSION}/libduckdb-linux-amd64.zip"
DUCKDB_CACHE="$WORK_DIR/.cache/libduckdb-${DUCKDB_VERSION}.zip"
FSST_PLUS_DIR="$WORK_DIR/fsst_plus"
CSV_OUT="$ROOT_DIR/benchmarking/csv/cf_speed_fsst_plus_vs_optfsst_plus.csv"
DATA_ROOT="$ROOT_DIR/data/refined"
REPS="${FSST_PLUS_BENCH_REPS:-5}"
PIN_CPU="${FSST_PLUS_BENCH_CPU:-0}"
MAX_BYTES="${FSST_PLUS_BENCH_MAX_BYTES:-268435456}"  # 256 MiB

step() { printf '\n==> %s\n' "$1"; }
fail() { printf 'Error: %s\n' "$1" >&2; exit 1; }
trap 'fail "setup_and_run.sh failed at line $LINENO"' ERR

for tool in git curl unzip cmake ninja g++ python3; do
    command -v "$tool" >/dev/null 2>&1 || fail "required tool not found: $tool"
done

mkdir -p "$WORK_DIR" "$WORK_DIR/.cache" "$(dirname "$CSV_OUT")"

if [[ ! -d "$DATA_ROOT" ]]; then
    fail "data root not found: $DATA_ROOT (run data/refine_datasets.py first)"
fi

step "Cloning fsst_plus into $FSST_PLUS_DIR"
if [[ -d "$FSST_PLUS_DIR/.git" ]]; then
    echo "  already present, skipping clone"
else
    git clone --depth=1 https://github.com/cwida/fsst_plus.git "$FSST_PLUS_DIR"
fi

step "Fetching prebuilt libduckdb (${DUCKDB_VERSION})"
if [[ ! -s "$DUCKDB_CACHE" ]]; then
    curl -sSL -o "$DUCKDB_CACHE" "$DUCKDB_ZIP_URL" || fail "failed to download $DUCKDB_ZIP_URL"
fi
DUCKDB_DST_INC="$FSST_PLUS_DIR/third_party/duckdb/src/include"
DUCKDB_DST_LIB="$FSST_PLUS_DIR/third_party/duckdb/build/release/src"
mkdir -p "$DUCKDB_DST_INC" "$DUCKDB_DST_LIB"
if [[ ! -s "$DUCKDB_DST_INC/duckdb.hpp" || ! -s "$DUCKDB_DST_LIB/libduckdb.so" ]]; then
    UNZIP_TMP="$WORK_DIR/.cache/libduckdb-${DUCKDB_VERSION}"
    rm -rf "$UNZIP_TMP" && mkdir -p "$UNZIP_TMP"
    unzip -q -o "$DUCKDB_CACHE" -d "$UNZIP_TMP"
    cp "$UNZIP_TMP/duckdb.hpp" "$DUCKDB_DST_INC/duckdb.hpp"
    cp "$UNZIP_TMP/duckdb.h"   "$DUCKDB_DST_INC/duckdb.h"
    cp "$UNZIP_TMP/libduckdb.so" "$DUCKDB_DST_LIB/libduckdb.so"
fi

step "Cloning upstream FSST into third_party/fsst"
if [[ ! -d "$FSST_PLUS_DIR/third_party/fsst/.git" ]]; then
    rm -rf "$FSST_PLUS_DIR/third_party/fsst"
    git clone --depth=1 https://github.com/cwida/fsst.git "$FSST_PLUS_DIR/third_party/fsst"
fi

step "Staging OptFSST sources into third_party/fsst_opt"
mkdir -p "$FSST_PLUS_DIR/third_party/fsst_opt"
for f in fsst.h fsst.cpp libfsst.cpp libfsst.hpp \
         fsst_avx512.cpp fsst_avx512.inc \
         fsst_avx512_unroll1.inc fsst_avx512_unroll2.inc \
         fsst_avx512_unroll3.inc fsst_avx512_unroll4.inc; do
    cp "$ROOT_DIR/$f" "$FSST_PLUS_DIR/third_party/fsst_opt/$f"
done
cp "$BENCH_DIR/fsst_opt_CMakeLists.txt" "$FSST_PLUS_DIR/third_party/fsst_opt/CMakeLists.txt"

step "Installing bench_runner sources and CMake glue into fsst_plus"
cp "$BENCH_DIR/bench_runner.cpp"   "$FSST_PLUS_DIR/src/bench_runner.cpp"
cp "$BENCH_DIR/fsst_opt_wrap.cpp"  "$FSST_PLUS_DIR/src/fsst_opt_wrap.cpp"
cp "$BENCH_DIR/fsst_plus_CMakeLists.txt" "$FSST_PLUS_DIR/CMakeLists.txt"

cat > "$FSST_PLUS_DIR/src/env.h" <<EOF
#pragma once
#include <string>
namespace env {
    inline const std::string project_dir = "${FSST_PLUS_DIR}";
}
EOF

step "Configuring vanilla FSST+ build"
cmake -S "$FSST_PLUS_DIR" -B "$FSST_PLUS_DIR/build" -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
step "Building vanilla bench_runner"
cmake --build "$FSST_PLUS_DIR/build" --target bench_runner -j"$(nproc)"

step "Configuring OptFSST+ build"
cmake -S "$FSST_PLUS_DIR" -B "$FSST_PLUS_DIR/build-opt" -G Ninja -DCMAKE_BUILD_TYPE=Release -DFSST_VARIANT=opt >/dev/null
step "Building OptFSST+ bench_runner"
cmake --build "$FSST_PLUS_DIR/build-opt" --target bench_runner -j"$(nproc)"

step "Running FSST+ vs OptFSST+ benchmark on $DATA_ROOT"
rm -f "$CSV_OUT"
LD_RUNPATH="$DUCKDB_DST_LIB" \
    python3 "$BENCH_DIR/run_bench.py" \
        --root "$DATA_ROOT" \
        --bench-fsst "$FSST_PLUS_DIR/build/bench_runner" \
        --bench-opt  "$FSST_PLUS_DIR/build-opt/bench_runner" \
        --ld-library-path "$DUCKDB_DST_LIB" \
        --out "$CSV_OUT" \
        --reps "$REPS" \
        --cpu "$PIN_CPU" \
        --max-bytes "$MAX_BYTES"

step "CSV written to $CSV_OUT"
wc -l "$CSV_OUT"
