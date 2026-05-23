#!/usr/bin/env python3
"""Run FSST+ vs OptFSST+ benchmark over all files in data/refined.

Pins the benchmark process to a single core (taskset) and runs both binaries
sequentially on the same input file. Each binary emits one CSV row per dataset
to stdout; we capture both rows and append them to the output CSV.

Usage:
    run_bench.py --root /home/hedi/btrfsst/data/refined \
                 --bench-fsst   ../build/bench_runner \
                 --bench-opt    ../build-opt/bench_runner \
                 --out          csv/fsstplus_vs_optfsstplus.csv \
                 [--reps 5] [--max-bytes 33554432] [--cpu 0]
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
from typing import Iterable, Optional

CSV_HEADER = [
    "variant",
    "dataset",
    "column",
    "n_strings",
    "raw_bytes",
    "compressed_bytes",
    "compression_factor",
    "compress_ms_mean",
    "decompress_ms_mean",
    "compress_mb_per_s_mean",
    "decompress_mb_per_s_mean",
]


def discover_corpora(root: Path) -> list[tuple[str, str, Path]]:
    """Return list of (dataset_name, column_name, path) for every leaf data file."""
    out: list[tuple[str, str, Path]] = []
    for path in sorted(root.rglob("*")):
        if not path.is_file():
            continue
        if path.suffix.lower() in {".pdf", ".png", ".pptx", ".mp4", ".db", ".csv"}:
            continue
        rel = path.relative_to(root)
        parts = rel.parts
        if len(parts) == 1:
            dataset = "root"
            column = parts[0]
        else:
            dataset = "/".join(parts[:-1])
            column = parts[-1]
        # Strip trailing .txt for cleaner column names.
        if column.endswith(".txt"):
            column = column[:-4]
        out.append((dataset, column, path))
    return out


def run_one(bench_bin: Path, ld_library_path: str, input_file: Path,
            dataset: str, column: str, variant_tag: str, reps: int,
            cpu: Optional[int], timeout_s: int) -> Optional[str]:
    cmd = [str(bench_bin), str(input_file), dataset, column, variant_tag, str(reps)]
    if cpu is not None:
        cmd = ["taskset", "-c", str(cpu)] + cmd
    env = os.environ.copy()
    env["LD_LIBRARY_PATH"] = ld_library_path
    try:
        proc = subprocess.run(
            cmd, capture_output=True, text=True, env=env, timeout=timeout_s
        )
    except subprocess.TimeoutExpired:
        sys.stderr.write(f"  TIMEOUT {variant_tag} on {dataset}/{column}\n")
        return None
    if proc.returncode != 0:
        sys.stderr.write(
            f"  FAIL {variant_tag} on {dataset}/{column}: rc={proc.returncode}\n"
            f"    cmd={' '.join(shlex.quote(x) for x in cmd)}\n"
            f"    stderr={proc.stderr.strip()[:400]}\n"
        )
        return None
    line = proc.stdout.strip().splitlines()
    if not line:
        return None
    return line[-1]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", required=True, type=Path)
    ap.add_argument("--bench-fsst", required=True, type=Path)
    ap.add_argument("--bench-opt", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--ld-library-path", required=True)
    ap.add_argument("--reps", type=int, default=5)
    ap.add_argument(
        "--max-bytes", type=int, default=256 * 1024 * 1024,
        help="skip files larger than this (guardrail against pathological inputs)",
    )
    ap.add_argument("--cpu", type=int, default=0, help="pin to this logical CPU (use -1 to disable)")
    ap.add_argument("--timeout", type=int, default=300)
    ap.add_argument("--limit", type=int, default=0, help="only run first N files (0 = all)")
    args = ap.parse_args()

    if not args.bench_fsst.exists():
        sys.exit(f"bench-fsst not found: {args.bench_fsst}")
    if not args.bench_opt.exists():
        sys.exit(f"bench-opt not found: {args.bench_opt}")

    corpora = discover_corpora(args.root)
    if args.limit:
        corpora = corpora[: args.limit]
    sys.stderr.write(f"discovered {len(corpora)} corpora under {args.root}\n")

    args.out.parent.mkdir(parents=True, exist_ok=True)
    write_header = not args.out.exists() or args.out.stat().st_size == 0
    cpu = None if args.cpu < 0 else args.cpu

    with args.out.open("a", newline="") as f:
        writer = csv.writer(f)
        if write_header:
            writer.writerow(CSV_HEADER)
            f.flush()

        for i, (dataset, column, path) in enumerate(corpora, start=1):
            size = path.stat().st_size
            tag = f"[{i}/{len(corpora)}] {dataset}/{column} ({size} B)"
            if size == 0:
                sys.stderr.write(f"  SKIP {tag} (empty)\n")
                continue
            if size > args.max_bytes:
                sys.stderr.write(f"  SKIP {tag} (>{args.max_bytes} B)\n")
                continue
            sys.stderr.write(f"{tag}\n")

            t0 = time.time()
            for bin_path, variant in (
                (args.bench_fsst, "FSSTplus"),
                (args.bench_opt, "OptFSSTplus"),
            ):
                row = run_one(
                    bin_path, args.ld_library_path, path, dataset, column,
                    variant, args.reps, cpu, args.timeout,
                )
                if row is None:
                    continue
                writer.writerow(row.split(","))
                f.flush()
            sys.stderr.write(f"  done in {time.time()-t0:.1f}s\n")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
