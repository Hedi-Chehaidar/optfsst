# buildDP-variant compression-speed benchmark

This benchmark compares the compression speed of OptFSST and OptFSST12
against otherwise identical builds where two micro-optimizations are stripped
from `SymbolTable::buildDP` (in `libfsst.hpp` for the 8-bit codec and
`libfsst12.hpp` for the 12-bit codec):

* the manually unrolled 8-deep cascade that walks the suffix trie one byte at
  a time, and
* the `likely` / `unlikely` (`__builtin_expect`) branch hints inside that
  cascade.

`buildDP` is the inner loop of OptFSST's DP parser. It is invoked once per
sample line during symbol-table training (`FSST_OPT_DP_TRAIN`) and once per
511-byte chunk during encoding (`FSST_OPT_DP_ENCODE`), so changing it touches
both `Optfsst_create` and `Optfsst_compress`.

## How the variants are built

A single compile-time switch, `BUILDDP_NAIVE`, selects the variant inside
`SymbolTable::buildDP` for both `libfsst.hpp` and `libfsst12.hpp`. The
production build is unaffected (the macro is only defined for the naive
library targets). Four library targets are compiled from the same source
trees:

* `fsst_opt`     — 8-bit OptFSST, production buildDP.
* `fsst_naive`   — 8-bit OptFSST, `-DBUILDDP_NAIVE`.
* `fsst12_opt`   — 12-bit OptFSST12, production buildDP.
* `fsst12_naive` — 12-bit OptFSST12, `-DBUILDDP_NAIVE`.

Four binaries (`dp_bench_opt`, `dp_bench_naive`, `dp_bench_opt12`,
`dp_bench_naive12`) link exactly one library each (the `Optfsst_*` symbols
collide between the 8-bit and 12-bit libraries, so they must not be linked
together). Every binary runs the full `Optfsst_create` + `Optfsst_compress`
pipeline with the OptFSST flag set
`FSST_OPT_DP_TRAIN | FSST_OPT_TRIPLES | FSST_OPT_PRUNE | FSST_OPT_DP_ENCODE`.

## Inputs and outputs

* Input corpus: every regular file under `data/refined/` (one UTF-8 string per
  line). Files with extensions `.pdf, .png, .pptx, .mp4, .db, .csv, .fsst` are
  skipped.
* Output CSVs (both written to `benchmarking/csv/`):
  * `dp_buildDP_variants.csv` — long-form, one row per `(variant, file)`:
    `variant, file, n_lines, raw_bytes, compressed_bytes,
     create_ms_mean, compress_ms_mean, total_ms_mean,
     create_mb_per_s_mean, compress_mb_per_s_mean, total_mb_per_s_mean`.
    `variant` ∈ `{opt, naive, opt12, naive12}`.
  * `dp_buildDP_speedup.csv` — derived pivot, one row per `(codec, file)`:
    `configuration, Speedup, file`, where `Speedup = naive_total_ms_mean /
    opt_total_ms_mean` and `configuration` ∈ `{OptFSST, OptFSST12}`. This is
    the input for the violin plot rendered by `plot_results.py
    dp_buildDP_speedup`.

For each file all four variants are timed in turn on the same pinned CPU. The
driver also asserts that paired variants produce the same `compressed_bytes`
within their codec (buildDP variants must be functionally equivalent — they
compute the same DP solution); any mismatch exits non-zero and is logged.

`--reps 5` is the default; the arithmetic mean is reported. Every binary is
pinned to one CPU via `taskset -c 0` to keep timings comparable.

## Metric conventions

Aligned with `benchmarking/runner.cpp` (paper benches):

* `raw_bytes` is the **file size on disk** (newlines included).
* `compressed_bytes` is `Σ out_lens[i] + fsst_export(encoder, …)` — sum of
  the encoded line bytes plus the serialised symbol table. This is the API-
  level payload size; it does not include any per-block framing because the
  bench drives OptFSST directly without going through a file format.
* `*_mb_per_s_mean` use **decimal MB** (`bytes / 1_000_000 / seconds`).

The one intentional departure from `runner.cpp` is CPU pinning
(`taskset -c 0`), kept here for timing repeatability.

## Running

```
bash benchmarking/dp_bench/setup_and_run.sh
```

Tunables (environment variables):

* `DP_BENCH_REPS` — repetitions per (variant, file). Default `5`.
* `DP_BENCH_CPU` — logical CPU to pin to (`-1` disables). Default `0`.
* `DP_BENCH_MAX_BYTES` — skip files larger than this. Default 256 MiB.

The build output lives in `build-dp-bench/`. To rebuild from scratch, delete
that directory.
