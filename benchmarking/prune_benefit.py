"""Rank files by --prune CF gain using existing improvement CSVs.

Reads csv/improvement.csv and csv/improvement12.csv produced by runner.cpp.
For each file, computes the multiplicative CF gain that --prune adds on top of
--triples, both on the dp-train-only track and on the dp-encode track.

Usage:
    python3 prune_benefit.py [--csv-dir csv] [--top N] [--bottom N]
                             [--sort {enc,train}] [--out PATH]
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import pandas as pd

TRACK_CONFIGS: dict[str, tuple[str, str, str]] = {
    "csv/improvement.csv": (
        "+ triples (dp-train)",
        "+ prune",
        "+ prune = OptFSST",
    ),
    "csv/improvement12.csv": (
        "+ triples (dp-train)",
        "+ prune",
        "+ prune = OptFSST12",
    ),
}

ENC_TRIPLES = "+ triples (dp-encode)"


def compute_prune_gain(csv_path: Path) -> pd.DataFrame:
    df = pd.read_csv(csv_path)
    pivot = df.pivot(index="file", columns="configuration", values="CF")

    key = f"csv/{csv_path.name}"
    if key not in TRACK_CONFIGS:
        raise ValueError(f"unknown improvement CSV: {csv_path}")
    triples_train, prune_train, prune_enc = TRACK_CONFIGS[key]

    missing = [c for c in (triples_train, prune_train, ENC_TRIPLES, prune_enc)
               if c not in pivot.columns]
    if missing:
        raise ValueError(f"missing configurations in {csv_path}: {missing}")

    result = pd.DataFrame(index=pivot.index)
    result["prune_gain_train"] = pivot[prune_train] / pivot[triples_train]
    result["prune_gain_enc"] = pivot[prune_enc] / pivot[ENC_TRIPLES]
    return result


def report(csv_path: Path, top: int, bottom: int, sort_key: str) -> pd.DataFrame:
    gains = compute_prune_gain(csv_path)
    sort_col = f"prune_gain_{sort_key}"
    ranked = gains.sort_values(sort_col, ascending=False)

    fmt = lambda v: f"{v:.4f}"
    print(f"\n=== {csv_path} — top {top} files by --prune CF gain "
          f"({sort_key} track) ===")
    print(ranked.head(top).to_string(float_format=fmt))
    print(f"\n=== {csv_path} — bottom {bottom} ===")
    print(ranked.tail(bottom).to_string(float_format=fmt))

    helps_train = (ranked.prune_gain_train > 1).sum()
    helps_enc = (ranked.prune_gain_enc > 1).sum()
    n = len(ranked)
    print(
        f"\nsummary: median train={ranked.prune_gain_train.median():.4f}  "
        f"median enc={ranked.prune_gain_enc.median():.4f}  "
        f"files where prune helps (train>1): {helps_train}/{n}  "
        f"(enc>1): {helps_enc}/{n}"
    )
    return ranked


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--csv-dir", default="csv", type=Path,
                        help="directory containing improvement[12].csv")
    parser.add_argument("--top", default=15, type=int)
    parser.add_argument("--bottom", default=10, type=int)
    parser.add_argument("--sort", default="enc", choices=("enc", "train"),
                        help="which prune track to rank by")
    parser.add_argument("--out", type=Path, default=None,
                        help="optional path to write combined ranked CSV")
    args = parser.parse_args(argv)

    csvs = [args.csv_dir / "improvement.csv",
            args.csv_dir / "improvement12.csv"]

    combined: list[pd.DataFrame] = []
    for csv_path in csvs:
        if not csv_path.exists():
            print(f"warning: {csv_path} not found, skipping", file=sys.stderr)
            continue
        ranked = report(csv_path, args.top, args.bottom, args.sort)
        if args.out is not None:
            tagged = ranked.copy()
            tagged.insert(0, "source", csv_path.name)
            combined.append(tagged)

    if args.out is not None and combined:
        pd.concat(combined).to_csv(args.out)
        print(f"\nwrote {args.out}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
