#!/usr/bin/env python3
"""Run the OptFSST buildDP-variant compression-speed benchmark over data/refined.

Pins both binaries to a single core (taskset) and runs them sequentially on
the same input file so that timings are comparable. Each binary emits one CSV
row per file to stdout; rows are appended to the output CSV.

Usage:
    run_bench.py --root /home/hedi/btrfsst/data/refined \
                 --bench-opt   ../build-dp-bench/dp_bench_opt \
                 --bench-naive ../build-dp-bench/dp_bench_naive \
                 --out         csv/dp_buildDP_variants.csv \
                 [--reps 5] [--max-bytes 268435456] [--cpu 0]
"""
from __future__ import annotations

import argparse
import csv
import os
import shlex
import subprocess
import sys
import time
from pathlib import Path
from typing import Optional

CSV_HEADER = [
    "variant",
    "file",
    "n_lines",
    "raw_bytes",
    "compressed_bytes",
    "create_ms_mean",
    "compress_ms_mean",
    "total_ms_mean",
    "create_mb_per_s_mean",
    "compress_mb_per_s_mean",
    "total_mb_per_s_mean",
]

SKIP_SUFFIXES = {".pdf", ".png", ".pptx", ".mp4", ".db", ".csv", ".fsst"}


def discover_corpora(root: Path) -> list[Path]:
    out: list[Path] = []
    for path in sorted(root.rglob("*")):
        if not path.is_file():
            continue
        if path.suffix.lower() in SKIP_SUFFIXES:
            continue
        out.append(path)
    return out


def run_one(bench_bin: Path, input_file: Path, variant_tag: str, reps: int,
            cpu: Optional[int], timeout_s: int) -> Optional[str]:
    cmd = [str(bench_bin), str(input_file), variant_tag, str(reps)]
    if cpu is not None:
        cmd = ["taskset", "-c", str(cpu)] + cmd
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout_s)
    except subprocess.TimeoutExpired:
        sys.stderr.write(f"  TIMEOUT {variant_tag} on {input_file}\n")
        return None
    if proc.returncode != 0:
        sys.stderr.write(
            f"  FAIL {variant_tag} on {input_file}: rc={proc.returncode}\n"
            f"    cmd={' '.join(shlex.quote(x) for x in cmd)}\n"
            f"    stderr={proc.stderr.strip()[:400]}\n"
        )
        return None
    lines = proc.stdout.strip().splitlines()
    if not lines:
        return None
    return lines[-1]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", required=True, type=Path)
    ap.add_argument("--bench-opt", required=True, type=Path)
    ap.add_argument("--bench-naive", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--reps", type=int, default=5)
    ap.add_argument(
        "--max-bytes", type=int, default=256 * 1024 * 1024,
        help="skip files larger than this (guardrail against pathological inputs)",
    )
    ap.add_argument("--cpu", type=int, default=0,
                    help="pin to this logical CPU (use -1 to disable)")
    ap.add_argument("--timeout", type=int, default=600)
    ap.add_argument("--limit", type=int, default=0,
                    help="only run first N files (0 = all)")
    args = ap.parse_args()

    if not args.bench_opt.exists():
        sys.exit(f"bench-opt not found: {args.bench_opt}")
    if not args.bench_naive.exists():
        sys.exit(f"bench-naive not found: {args.bench_naive}")

    corpora = discover_corpora(args.root)
    if args.limit:
        corpora = corpora[: args.limit]
    sys.stderr.write(f"discovered {len(corpora)} corpora under {args.root}\n")

    args.out.parent.mkdir(parents=True, exist_ok=True)
    write_header = not args.out.exists() or args.out.stat().st_size == 0
    cpu = None if args.cpu < 0 else args.cpu

    mismatch_count = 0
    with args.out.open("a", newline="") as f:
        writer = csv.writer(f)
        if write_header:
            writer.writerow(CSV_HEADER)
            f.flush()

        for i, path in enumerate(corpora, start=1):
            size = path.stat().st_size
            tag = f"[{i}/{len(corpora)}] {path} ({size} B)"
            if size == 0:
                sys.stderr.write(f"  SKIP {tag} (empty)\n")
                continue
            if size > args.max_bytes:
                sys.stderr.write(f"  SKIP {tag} (>{args.max_bytes} B)\n")
                continue
            sys.stderr.write(f"{tag}\n")

            rows: dict[str, list[str]] = {}
            t0 = time.time()
            for bin_path, variant in (
                (args.bench_opt, "opt"),
                (args.bench_naive, "naive"),
            ):
                row = run_one(bin_path, path, variant, args.reps, cpu, args.timeout)
                if row is None:
                    continue
                fields = row.split(",")
                writer.writerow(fields)
                f.flush()
                rows[variant] = fields

            if "opt" in rows and "naive" in rows:
                # compressed_bytes is column index 4 in CSV_HEADER.
                if rows["opt"][4] != rows["naive"][4]:
                    mismatch_count += 1
                    sys.stderr.write(
                        f"  CF MISMATCH on {path}: opt={rows['opt'][4]} "
                        f"naive={rows['naive'][4]}\n"
                    )
            sys.stderr.write(f"  done in {time.time()-t0:.1f}s\n")

    if mismatch_count:
        sys.stderr.write(
            f"WARNING: {mismatch_count} files produced different compressed sizes "
            "between variants — buildDP variants must be functionally equivalent.\n"
        )
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
