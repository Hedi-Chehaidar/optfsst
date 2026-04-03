# AVX-512 DP Improvement Proposals

Verified against the current tree on 2026-04-03 by reading the scalar DP path, the AVX-512 DP path, and the current DP consumers, then confirming the project still builds with `cmake --build build -j2`.

Relevant code:
- [libfsst.hpp](/home/stoian/btrfsst/libfsst.hpp#L103)
- [libfsst.hpp](/home/stoian/btrfsst/libfsst.hpp#L285)
- [libfsst.hpp](/home/stoian/btrfsst/libfsst.hpp#L371)
- [libfsst.hpp](/home/stoian/btrfsst/libfsst.hpp#L402)
- [fsst_avx512.cpp](/home/stoian/btrfsst/fsst_avx512.cpp#L150)
- [libfsst.cpp](/home/stoian/btrfsst/libfsst.cpp#L403)
- [libfsst.cpp](/home/stoian/btrfsst/libfsst.cpp#L935)

## 1. Fix the tie-break mismatch before tuning further

This is the most important item because it is not only a performance detail.

The scalar DP takes a candidate when:

`cost < bestCost || (cost == bestCost && len >= chosenLen)`

See [libfsst.hpp](/home/stoian/btrfsst/libfsst.hpp#L420). Because the loop walks candidates in bucket order, equal-cost and equal-length ties are resolved in favor of the later candidate encountered.

The AVX-512 path does not implement that same rule. It packs:

`key = (cost << 12) | ((8 - len) << 9) | code`

and then takes the lane-wise minimum; see [libfsst.hpp](/home/stoian/btrfsst/libfsst.hpp#L103) and [fsst_avx512.cpp](/home/stoian/btrfsst/fsst_avx512.cpp#L213). For equal cost and equal length, that picks the smaller code, not the later candidate.

Why this matters:
- `dpChoice` is consumed both during training and during final emission, so different tie resolution can change both the learned counts and the emitted parse; see [libfsst.cpp](/home/stoian/btrfsst/libfsst.cpp#L403) and [libfsst.cpp](/home/stoian/btrfsst/libfsst.cpp#L935).

Proposal:
- Either make the scalar and AVX-512 paths share one canonical tie-break rule, or explicitly encode the desired traversal order into the AVX key.
- If you want AVX-512 to match current scalar behavior exactly, add a monotonic per-candidate order field and prefer the larger order on exact ties.

## 2. Remove `candValid64` from the hot AVX-512 loop

Right now each 8-candidate chunk does all of this:
- load `candValid64`
- compare against zero
- mask out padded entries

See [fsst_avx512.cpp](/home/stoian/btrfsst/fsst_avx512.cpp#L192).

But the amount of padding is already known per bucket through `count` and `count8`; see [libfsst.hpp](/home/stoian/btrfsst/libfsst.hpp#L225) and [libfsst.hpp](/home/stoian/btrfsst/libfsst.hpp#L371).

Proposal:
- Vectorize all full 8-wide chunks with no validity array at all.
- Handle only the final partial chunk with a tail mask derived from `bk.count & 7`, or precompute one tail mask per bucket.

Why this looks worthwhile:
- it removes one 64-byte load, one compare, and one extra mask operation from every vector iteration of the DP inner loop
- it also lets you drop `candValid64` storage entirely from the bucket data structure

## 3. Keep the DP window as rolling state instead of rebuilding it every byte

The current AVX-512 loop rebuilds:

`dpWin = [dpCost[i+1], ..., dpCost[i+8]]`

from scalar memory every iteration; see [fsst_avx512.cpp](/home/stoian/btrfsst/fsst_avx512.cpp#L182).

But because the DP runs backwards, the next iteration's window is just:

`[dpCost[i], dpCost[i+1], ..., dpCost[i+7]]`

Proposal:
- initialize `dpWin` once near the tail
- after computing `dpCost[i]`, shift the 8 active lanes by one and insert the newly computed cost into lane 0

Why this is credible:
- the dependency pattern is perfectly regular
- it removes repeated scalar gathers of 8 DP values from the hot loop
- it should reduce pressure on both the frontend and load ports without changing the algorithm

## 4. Replace `load_u64_zero_padded()` in the reverse scan with a rolling 64-bit word

Both scalar and AVX-512 DP currently call `load_u64_zero_padded(data + i, n - i)` on every position; see [libfsst.hpp](/home/stoian/btrfsst/libfsst.hpp#L82), [libfsst.hpp](/home/stoian/btrfsst/libfsst.hpp#L414), and [fsst_avx512.cpp](/home/stoian/btrfsst/fsst_avx512.cpp#L170).

For a reverse scan, the next word is derivable from the previous one:

`w(i-1) = ((u64)data[i-1]) | (w(i) << 8)`

under the same little-endian layout already used by the helpers.

Proposal:
- seed `w` once at the end of the string
- update it with a shift-and-insert when moving from `i` to `i-1`

Why this looks worthwhile:
- it removes a `memcpy`-based helper call from the hottest loop in both DP versions
- it should especially help short strings, where helper overhead is a larger fraction of total work

## Suggested validation order

1. Fix the tie-break rule first and verify AVX-512 vs scalar `dpChoice` equality on the same input.
2. Then benchmark item 2 and item 3 independently.
3. Benchmark item 4 last, because it affects both scalar and AVX-512 code paths and is easiest to measure in isolation.
