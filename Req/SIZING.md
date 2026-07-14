# SIZING.md — solution size, validation cost, memory across parameters

A table of solution size, verification effort, and memory requirements for
Equihash and Requihash, at k ∈ {5, 7, 9}, across a range of n from a
sub-millisecond validation case to parameters requiring hundreds of GB to
mine. Same core assumptions throughout (SPEC.md's construction); naive
full-materialization memory alongside the compact index-pointer designs for
both variants.

## 1. Method and evidence grades

Four figures per row, each with a distinct evidence grade — stated per column,
not implied:

| Column | Formula | Grade |
|---|---|---|
| `ell` (collision bit length) | `n / (k+1)` | Definitional |
| Init list size `N` | `2^(ell+1)` | Definitional |
| Solution size (minimal / compact) | `2^k·(ell+1)/8` and `2^k·ell/8` bytes | **Measured** — computed by `Params::solution_width`/`compact_width` in `rust/src/lib.rs`, exercised by the `table3_wire_sizes` test |
| Verify hashes (m=1) | `2^k` | Definitional; the `m` multiplier (SPEC.md §5–6) scales this linearly and is orthogonal to the table below |
| Naive peak memory | `N · (n/8 + 4)` bytes | **Measured shape** — this is what the `reference`/`arena` solvers in `rust/src/solve/` actually allocate at round 0 (full n-bit hash row + one u32 leaf index); BENCHMARK.md's §2 profiling confirms this dominates naive solver cost. Later rounds have fewer, larger rows; round-0 is reported as the peak because index-list growth roughly offsets row-count shrinkage in a birthday-style merge, and it is the figure this repo's solvers actually hit first |
| Index-pointer peak, Equihash | Linear-in-`(n·N)` bits, calibrated to the ~49 MB figure at (200,9) cited in `~/Work/ZK/ZKs/Equihash.md` (from Tang–Sun–Gong's Proposition 4, O(n·N) bits) | **Derived, calibrated** — this repo has not implemented the index-pointer solver (PLAN.md A6, not started), so this column is a model anchored to one external citation, not an independent measurement. Treat as order-of-magnitude, not exact |
| Index-pointer peak, Requihash | `((k²+5k+2)/4·ell + 2^(k-1)) · N / 8` bytes — Tang–Sun–Gong's Proposition 3 closed form, exactly as cited in `~/Work/ZK/ZKs/Equihash.md` | **Formula, computed exactly** — no calibration needed; this is the paper's own stated asymptotic form evaluated directly. Reproduces the paper's cited 224 MB at (200,9) (previously rounded to "223 MB" in this repo's notes) |

**What this table is not:** a benchmark. No wall-clock time appears. It is a
memory/size *sizing* reference — how big each artifact gets — separate from
the throughput numbers in BENCHMARK.md.

## 2. The table

k = 5, 7, 9; n swept from a trivial validation case to parameters whose naive
peak memory reaches multi-TB (the "hundreds of GB" case sits mid-table for
each k; going further is definitional continuation, not a new regime).

| k | n | ell | init list N | sol size (min/compact) | verify hashes (m=1) | naive peak mem | index-pointer peak, Equihash (derived) | index-pointer peak, Requihash (formula) |
|---|---|---|---|---|---|---|---|---|
| 5 | 24 | 4 | 2^5 | 20/16 B | 32 | 224 B | 94 B | 272 B |
| 5 | 48 | 8 | 2^9 | 36/32 B | 32 | 5.0 KB | 2.9 KB | 7.5 KB |
| 5 | 96 | 16 | 2^17 | 68/64 B | 32 | 2.0 MB | 1.5 MB | 3.5 MB |
| 5 | 144 | 24 | 2^25 | 100/96 B | 32 | 704 MB | 564 MB | 1.28 GB |
| 5 | 168 | 28 | 2^29 | 116/112 B | 32 | 12.5 GB | 10.3 GB | 23.8 GB |
| 5 | 192 | 32 | 2^33 | 132/128 B | 32 | 224 GB | 188 GB | 432 GB |
| 5 | 216 | 36 | 2^37 | 148/144 B | 32 | 3.9 TB | 3.3 TB | 7.6 TB |
| 7 | 32 | 4 | 2^5 | 80/64 B | 128 | 256 B | 125 B | 600 B |
| 7 | 96 | 12 | 2^13 | 208/192 B | 128 | 128 KB | 94 KB | 322 KB |
| 7 | 168 | 21 | 2^22 | 352/336 B | 128 | 100 MB | 82 MB | 258 MB |
| 7 | 200 | 25 | 2^26 | 416/400 B | 128 | 1.81 GB | 1.53 GB | 4.70 GB |
| 7 | 232 | 29 | 2^30 | 480/464 B | 128 | 33.0 GB | 28.4 GB | 85.9 GB |
| 7 | 264 | 33 | 2^34 | 544/528 B | 128 | 592 GB | 517 GB | 1.51 TB |
| 7 | 296 | 37 | 2^38 | 608/592 B | 128 | 10.25 TB | 9.06 TB | 26.9 TB |
| 9 | 40 | 4 | 2^5 | 320/256 B | 512 | 288 B | 157 B | 1.5 KB |
| 9 | 120 | 12 | 2^13 | 832/768 B | 512 | 152 KB | 118 KB | 640 KB |
| 9 | 200 | 20 | 2^21 | 1344/1280 B | 512 | 58.0 MB | 49.0 MB | 224 MB |
| 9 | 240 | 24 | 2^25 | 1600/1536 B | 512 | 1.06 GB | 941 MB | 4.0 GB |
| 9 | 280 | 28 | 2^29 | 1856/1792 B | 512 | 19.5 GB | 17.2 GB | 72.0 GB |
| 9 | 320 | 32 | 2^33 | 2112/2048 B | 512 | 352 GB | 314 GB | 1.25 TB |
| 9 | 360 | 36 | 2^37 | 2368/2304 B | 512 | 6.12 TB | 5.51 TB | 22.0 TB |

The (200,9) row is the calibration anchor for the Equihash-derived column
(49.0 MB exactly, by construction) and independently reproduces the paper's
cited Requihash figure (224 MB vs. the "~223 MB" previously noted in
`Equihash.md` — rounding-level agreement, confirming the formula transcription
is correct).

## 3. Readings

- **Solution size and verify cost are k-only, n-independent in count** (32,
  128, 512 hashes for k=5,7,9 respectively) but grow linearly in bytes with n
  — the (n, k) split is exactly the "verification cheapness vs. memory
  hardness" dial the algorithm exposes, visible directly in column 4 vs. 6.
- **Requihash costs 2–5× the Equihash index-pointer memory at matched (n,k)**
  across the whole sweep — consistent with the F-A4 finding (`Equihash.md`)
  that regularity at least doubles peak memory for k ≥ 5. Holding n=200 fixed
  and varying k (k=7: 3.07×; k=9: 4.57×) isolates the k-dependence: the ratio
  *grows* with k, which the `(k²+5k+2)/4` term in Proposition 3 predicts
  directly — quadratic in k, against the Equihash-side model's linear-in-n
  scaling, which carries no k-dependence at all. (k=5 has no n=200 row since
  200 isn't a multiple of k+1=6; the nearest comparable point, n=144, gives
  2.32×, consistent with the trend.)
- **The naive/index-pointer gap shrinks as n grows, and is smaller everywhere
  than the historical record's headline collapse.** At the smallest validation
  parameters the index-pointer design still saves 45–58% of memory (e.g.
  k=5/n=24, k=9/n=40); by the largest rows in this sweep the saving has
  narrowed to 10–15% (k=5/n=216: 14.6%; k=9/n=360: 10.0%) — because as n grows,
  the fixed 4-byte leaf-index term in the naive row shrinks relative to the
  growing n-bit hash payload, so there is proportionally less for the
  index-pointer trick to remove. Neither end resembles the dramatic collapse
  the historical record (§9 of `Equihash.md`) associates with the 2016
  optimization wave — that collapse was against a much less efficient
  *original* reference implementation (pre-truncation, ~1 GB at (200,9) before
  any optimization); this table's "naive" column already assumes the
  truncated-index representation this repo's solvers use, so the remaining
  index-pointer win shown here is the *residual* optimization, not the whole
  historical delta.
- **The "hundreds of GB" case** lands at k=5/n=192, k=7/n=264, k=9/n=320 —
  each a legitimate n%（k+1)==0, n%8==0 parameter choice, none deployed by any
  known chain (the Equihash-family ecosystem survey in `~/Work/ZK/ZKs/Equihash.md`
  §4 tops out at (200,9) and (144,5); nothing in that table approaches this
  memory class).

## 4. What this table does not answer

Time cost (steepness under memory reduction — PLAN.md A5, not started);
whether the "naive" solvers in this repo actually *achieve* the modeled peak
at large n (only measured up to (96,5)/(144,5) per BENCHMARK.md; the TB-scale
rows are pure arithmetic, unexercised); and the Equihash-derived column's
accuracy away from its single calibration point (it is a straight-line model,
not a second independent formula — a real risk if the true asymptotic has a
different shape than "linear in n·N" once other terms are accounted for).
