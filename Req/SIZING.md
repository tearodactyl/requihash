# SIZING.md — solution size, verification cost, memory across parameters

Four kinds of sizing information, kept deliberately separate so each can be
judged on its own evidence: **§1** the Equihash background — terminology,
granularity, the formulas, and the practical measures on record from the
implementers (Khovratovich's reference, xenoncat, tromp, and this repo's
own); **§2** the Requihash/Sequihash memory question, laid out as parallel
computations with their assumptions, calculated and measured, without
adjudicating between them; **§3** what this repo has actually measured;
**§4** the parameter tiers this project works in, from trace/debug scale to
the memory-hard top of the implementable family. Same core assumptions
throughout (SPEC.md's construction).

## 0. Naming note

The paper (Lili Tang, Yao Sun, Xiaorui Gong, [eprint 2025/1351](https://eprint.iacr.org/2025/1351),
"On the Regularity of the Generalized Birthday Problem" — full citation and
companion 2025 papers by overlapping authors: `~/Work/ZK/Requihash/PAPERS.md`)
names this construction **Sequihash** throughout its own text, code, and
artifact repo. This project keeps its own established name, **Requihash**,
deliberately — this project's implementation predates a careful reading of
the paper's own naming, and renaming now would cost more clarity than it
buys. Mentioned here only so a reader searching upstream sources knows to
search "Sequihash" or the eprint number, not "Requihash": checked directly,
search results for "Sequihash" and for the eprint number turn up nothing
beyond the paper itself and its two artifact repos — no independent
implementations, no third-party citations, no community discussion found as
of 2026-07-13/14.

**A real, executed correctness check against the paper's own code, not just
its published tables:** the paper's `GBP-solver/k_list_algorithm.py` (in the
`tl2cents/Generalized-Birthday-Problem` repo) is a runnable Wagner k-list
solver — not a KAT/test-vector table; the paper publishes no fixed
input/output pairs anywhere. Run at a tiny parameter point (n=24, k=2^3, a
fixed nonce): it found 3 solutions and self-verified one (`hashval == 0`
XOR check passed). This confirms the solver code is real and runs, and gives
one genuinely executed reference point — it does not, by itself, validate
any of the memory/time figures below, which come from the paper's
*published tables*, a separate artifact.

## 0a. Wagner-lineage predecessor papers (memory/time tradeoff background)

Two earlier papers on Wagner's own generalized birthday algorithm, read for
background on the memory/time tradeoff surface this document is about —
neither is the source of any figure below, but both ground the "memory can
be traded for time/machines" framing that recurs in `SECURITY_ANALYSIS.md`'s
TMTO discussion:

- Bernstein, "Better price-performance ratios for generalized birthday
  attacks," 2007-09-04 —
  [paper](https://cr.yp.to/papers.html#genbday) (permanent ID
  `7cf298bebf853705133a84bea84d4a07`). Improves the exponents on Wagner's
  original attack (machine-size/time tradeoff), parametrized by `k = 2^(i-1)`.
- Bernstein, Lange, Niederhagen, Peters, Schwabe, "FSBday: Implementing
  Wagner's generalized birthday attack against the SHA-3 round-1 candidate
  FSB," 2011-09-27 —
  [paper](https://cr.yp.to/papers.html#fsbday) (permanent ID
  `ded1984108ff55330edb8631e7bc410c`). A real, executed implementation
  against FSB48 — the paper's own headline number is illustrative of exactly
  this document's theme: a straightforward implementation of Wagner's attack
  "would need 20 TB of storage," reduced to running on a cluster of 8
  machines with 8 GB RAM / 700 GB disk each via a parallelized, disk-backed
  redesign. A concrete precedent for "naive full-memory formula vs. real
  achievable footprint" outside the Equihash/Sequihash line entirely.

Wagner's own original paper ("A Generalized Birthday Problem," CRYPTO 2002)
is cited by `~/Work/ZK/Requihash/Equihash.md` via its
[live listing](https://people.eecs.berkeley.edu/~daw/papers/genbday.html);
no local PDF of it has been located in this project's reference directories
as of this check.

## 1. Equihash sizing background

### 1a. Terminology and granularity

- **ell** (also written ℓ or cbl, "collision bit length") = `n/(k+1)`: the
  bits collided per round. **cbyte** = `ceil(ell/8)`: its byte width.
- **Initial list** `N = 2^(ell+1)` items ("leaves"). Each round pairs
  colliding rows, so the expected list size stays ~N through all k rounds.
- **Row**: one working item — a hash payload plus provenance. The hash
  payload as *implemented* is the **expanded width** `(k+1)·cbyte` bytes
  (each ell-bit segment padded to a byte boundary), not the raw `n/8`.
- **Provenance representations**, the axis every memory number depends on:
  *full-index* (each row carries all `2^round` leaf indices it descends
  from — what this repo's reference/arena/bucket solvers do), *index-pointer*
  (each row carries two pointers to the previous round's rows; full indices
  reconstructed only at solution time — the decisive 2016-17 technique,
  `solve/pointer.rs` prototype, PLAN T2.4), and *truncated/hybrid* forms in
  between (what the paper's propositions model).
- **Granularity of "memory"**: payload bits (what formulas count), allocated
  bytes (what a counting allocator sees — includes container headers and
  malloc rounding), and resident set (what the OS sees). The three can
  differ by an order of magnitude; every figure below states which it is.
  `BENCH.md` requires the two instruments for measured numbers.

### 1b. Definitional formulas

| Quantity | Formula | Notes |
|---|---|---|
| ell | `n/(k+1)` | valid range [8, 25] — REVIEW_REQ.md F14; `Params::n_bounds(k)` computes valid n per k |
| Initial list N | `2^(ell+1)` | bounded ≤ 2^26 by ell ≤ 25 |
| Solution size (minimal) | `2^k·(ell+1)/8` B | computed by `Params::solution_width`, test-exercised |
| Solution size (compact) | `2^k·ell/8` B | Requihash wire form, `Params::compact_width` |
| Verify hashes (m=1) | `2^k` | k-only; the `m` multiplier (SPEC.md §5-6) scales linearly, orthogonal |

Valid n per k (multiples of `lcm(8, k+1)` in `[8(k+1), min(25(k+1), 512)]`,
proven exhaustively equivalent to constructor acceptance):

| k | valid n (lo–hi, step) | k | valid n (lo–hi, step) |
|---|---|---|---|
| 1 | 16–48, 8 | 6 | 56–168, 56 |
| 2 | 24–72, 24 | 7 | 64–200, 8 |
| 3 | 32–96, 8 | 8 | 72–216, 72 |
| 4 | 40–120, 40 | 9 | 80–240, 40 |
| 5 | 48–144, 24 | 63 | 512 only (largest valid k) |

### 1c. The "naive peak memory" formula, explained

This repo's reference formula is

    naive peak = N · (n/8 + 4) bytes

Model: a single snapshot of the initial list, where every item carries its
full n-bit hash (`n/8` bytes) plus one 4-byte leaf index — payload only.
What it deliberately does **not** model, each of which real solvers pay:

1. **Expanded width** — implementations store `(k+1)·cbyte ≥ n/8` bytes per
   row (byte-padding per segment), not the raw hash.
2. **Index growth** — in the full-index representation a surviving row at
   round r carries `2^r` indices; the formula counts exactly one, forever.
3. **Allocator overhead** — container headers and malloc rounding; measured
   at ~443 actual bytes per row against the formula's 16 at (96,5) (§3).
4. **Round double-buffering** — input and output lists coexist during a
   merge round.

Its character is therefore *an idealized payload floor, not a prediction*:
real full-index solvers measure 20–35× above it (§3), while the paper's
index-pointer accounting lands only ~1.2× *below* it at (200,9) (48.5 MB
published vs. 58 MB naive) — the formula is already close to compact-
representation territory, which is why it must not be read as a "no
optimization" baseline.

The four costs *are* modelable, and the gap closes almost entirely. The
peak is the coexistence of the last two row generations (double-buffered
round k−1 → k), whose rows carry 2+1 remaining `cbyte` segments and
`4·(2^(k−1) + 2^k)` index bytes, plus ~96 B/row of container headers:

    full-index model = N · (3·cbyte + 12·2^(k−1) + 96) bytes

Validation against §3's counting-allocator measurements (measured/predicted):
(48,5) 174.7 KB / 145.5 KB = **1.20×**; (72,5) 2.00 MB / 2.30 MB =
**0.87×**; (96,5) 55.4 MB / 36.75 MB = **1.51×**; RK's independent (120,4)
10.5 GB / 6.28 GB = **1.67×** (different codebase). A 0.87–1.5× band in
place of the floor's 20–35× understatement; the residual spread is
allocator size-class rounding, `Vec` growth-doubling slack, survivor-count
variance, and backend layout differences. The per-cost scaling: expansion
is per-ell (≤1.33×), the `12·2^(k−1)` index term is per-k and exponential
(the dominant cost, and exactly what index-pointer storage eliminates —
PLAN T2.4), headers are constant per row, double-buffering is the
adjacent-generations sum (removable by in-place merge, the fourth 2016-17
technique, not applied here). Implemented as the `full-index(model)` column
in `scripts/equihash_formulas.py`; grade: modeled, calibrated at three
points, full-index representations only.

### 1d. Practical measures on record

The measured/stated footprints from the implementers this project tracks —
these are *allocated/resident* numbers for real software, a different kind
of quantity from every formula above:

| Source | (n,k) | Figure | Nature |
|---|---|---|---|
| xenoncat (challenge winner) | (200,9) | **178 MB** ("15×11862016 bytes") | author-stated, assembly solver — `SOLVERS.md` §0.3 |
| tromp `equi_miner` | (200,9) | **144 MB** ("compared to xenoncat's 178MB") | author-stated; 2^12 buckets vs xenoncat's 2^16 + layout gains |
| tromp (attributed) | (144,5) | **~2.5 GB** | attributed to tromp by eprint 2025/2141; multi-threading "crucial for 144,5" per tromp |
| eprint 2025/2141 (own impl) | (144,5) | **1.45 GB** baseline; **700 MB** at ~2× time | authors' claim, code at `tl2cents/Wagner-Algorithms`, not yet run here |
| Khovratovich reference (RK port) | (120,4) | **10.5 GB**, 162 s | measured by this project (single run) — the reference algorithm's full-index cost, `SOLVER_CORPUS.md` RK |
| This repo, `solve_reference`/`solve_arena` | (40,4)–(96,5) | §3 table | measured, counting allocator, ≥2 params |

Spread worth internalizing: at (200,9) the same problem costs 144–178 MB in
tuned index-pointer implementations, ~49 MB in the paper's asymptotic
accounting, and 58 MB in this repo's payload-floor formula — three
different quantities that only look comparable. The 144–178 MB vs 49 MB gap
(~3–3.6×) is implementation overhead vs. big-O estimate; not yet decomposed
(PLAN T3 open investigations).

## 2. Requihash and Sequihash: the computations, laid side by side

The published memory figures come from **three distinct computations in the
paper plus this repo's formula**. They embody different assumptions; this
document presents them in parallel and does not adjudicate. Where the
assumptions are not stated by the paper, what can be gleaned is marked as
inference.

| Computation | Formula (bits) | At (200,9) | Assumptions (stated or gleaned) |
|---|---|---|---|
| Equihash — Table 3 / Proposition 4 | `O(n·N)` at constant 1 | 2^28.6 ≈ **48.5 MB** | single-list algorithm **with index pointers**; the paper's headline accounting. Reproduces every published Table 3 Equihash row to ±0.04 log2 bits |
| Equihash — Proposition 7 (appendix D.2) | `2·(n+k−ℓ−1)·N` | ≈ **94 MB** | a fuller per-row accounting (hash remainder + provenance both carried, double-buffered — gleaned, not stated). The paper does not connect it to Table 3; treated here as a different computation for a more detailed strategy, not as an error |
| Sequihash — Table 3 / Proposition 6 | `((k²+5k+2)/4·ℓ + 2^(k−1))·N` | 2^30.8 ≈ **223 MB** | k-list algorithm **with index trimming** (the best surviving strategy once regularity disables single-list pointers). Reproduces every published Table 3 Sequihash row to ±0.04 log2 bits |
| This repo — naive floor (§1c) | `N·(n+32)` | **58 MB** | payload-only snapshot; neither variant-specific nor implementation-faithful |

Both Table 3 formulas were validated against **all seven** published rows
before being used to extrapolate anywhere; every prediction matched to
within the paper's one-decimal rounding.

### 2a. The paper's own published Table 3, verbatim (page 31 of the PDF)

| (n, K) | Equihash Time | Equihash Mem. | Equihash Sol-Size | Sequihash Time | Sequihash Mem. | Sequihash Sol-Size |
|---|---|---|---|---|---|---|
| (96, 2^5) | 2^19.3 | 2^23.6 | 68 B | 2^22.6 | 2^24.8 | 64 B |
| (128, 2^7) | 2^19.8 | 2^24.0 | 272 B | 2^24.6 | 2^25.7 | 256 B |
| (160, 2^9) | 2^20.2 | 2^24.3 | 1088 B | 2^26.6 | 2^26.6 | 1024 B |
| (144, 2^5) | 2^27.3 | 2^32.2 | 100 B | 2^30.6 | 2^33.4 | 96 B |
| (150, 2^5) | 2^28.3 | 2^33.2 | 104 B | 2^31.6 | 2^34.4 | 100 B |
| (200, 2^9) | 2^24.2 | 2^28.6 | 1344 B | 2^30.6 | 2^30.8 | 1280 B |
| (288, 2^8) | 2^36.0 | 2^41.2 | 1056 B | 2^41.6 | 2^42.9 | 1024 B |

In MB/GB (bits ÷ 8, verified by direct computation — memory columns are bit
counts per the paper's convention): (96,5) **1.5 / 3.5 MB**; (128,7)
**2.0 / 6.5 MB**; (160,9) **2.5 / 12.1 MB**; (144,5) **588 MB / 1.3 GB**;
(150,5) **1.1 / 2.6 GB**; (200,9) **48.5 / 223 MB**; (288,8)
**294 / 955 GB**. (The narrative text rounds (200,9) to "49 MB"/"223 MB".)

Note two published rows lie **outside this implementation's parameter
bounds** and appear only here, never in §4's tiers: (150,5) fails `n % 8`,
(288,8) has ell = 32 > 25 (F14). The paper's math does not carry this
implementation's byte-alignment and accumulator constraints.

### 2b. Requihash-to-Equihash memory ratio, from published rows only

Computed directly from Table 3, not extrapolated: (96,5) and (144,5) both
**2.30×**; (128,7) and (288,8) both **3.25×**; (160,9) **4.92×**; (200,9)
**4.59×** (the paper's own "factor of 4.6"). The k=5 rows cluster near
2.3×, k=7 near 3.25×, k=9 near 4.6-4.9× — "the ratio grows with k" is
directionally supported, but it also varies with n within each k, so no
closed form is asserted from seven points.

### 2c. Solution size and verification, both variants

Solution size and verify cost are k-only in count (2^k hashes: 32, 128, 512
for k = 5, 7, 9) and grow linearly in bytes with n. Requihash's compact
encoding is always exactly `2^k/8` bytes *smaller* than Equihash's minimal
encoding (one disambiguation bit per index removed): at (200,9), 1344 →
1280 bytes. Confirmed measured by both implementations' wire tests.

## 3. Measured in this repo (counting allocator)

`rust/src/bin/req_memcheck.rs`, a global counting allocator wrapped around
this repo's own `solve_reference` and `solve_arena`, executed at real
parameters (`cargo run --release --bin req_memcheck`). This is the one
section of this document backed by executed code in this repo rather than a
transcribed or computed figure.

**F14 correction (2026-07-17, `REVIEW_REQ.md`):** the (24,5) row below is
structurally degenerate — ell = 4 < 8 under-runs `expand_array`'s
one-extraction-per-byte design, leaving the tail of every expanded row zero
(identically in both implementations, so it self-consistently solved and
verified, but it is not real 4-bit Wagner). `Params` now rejects ell outside
[8, 25], so (24,5) is no longer constructible; the row is kept only for the
historical trail, and the former "20–52×" headline's 52× endpoint is
suspect — T2.2 re-anchors the small end at (40,4) (already the new
`req_memcheck` default).

| (n,k) | naive floor (§1c) | measured peak, `solve_reference` | measured peak, `solve_arena` | ratio (reference) |
|---|---|---|---|---|
| (24,5) *invalid, historical* | 224 B | 11.4 KB | 8.6 KB | 52.0× |
| (48,5) | 5.0 KB | 174.7 KB | 164.8 KB | 34.9× |
| (72,5) | 104.0 KB | 2.00 MB | 2.07 MB | 19.7× |
| (96,5) | 2.00 MB | 55.4 MB | 66.0 MB | 27.7× |

At (96,5) this is ~443 measured bytes per row against the formula's 16 —
consistent with BENCHMARK.md's independent time-profiling finding that
per-row heap allocation is ~59% of solve time; two instruments, one
conclusion. **Measurement does not yet extend past (96,5)**; every larger
figure in §4 is computed, not measured (T2.2, run gated on explicit
approval).

## 4. Parameter tiers

The working parameter set, organized by what each tier is *for*. Columns:
naive floor (§1c), Equihash by Table 3/P4, Requihash by P6 — computed
values, evidence grades per §2; "heritage" marks deployed-chain parameters.
`scripts/equihash_formulas.py` `SWEEP_POINTS` mirrors these rows exactly
(kept in sync in the same pass, per the A20 discipline).

### Tier 0 — trace/debug (sub-millisecond, fully traceable)

512 leaves; a whole solve fits in a debugger session or a printed trace.

| (n,k) | ell | N | sol (min/compact) | verify | naive | Eq-P4 | Req-P6 |
|---|---|---|---|---|---|---|---|
| (40,4) | 8 | 2^9 | 18/16 B | 16 | 4.5 KB | 2.5 KB | 5.25 KB |
| (48,5) | 8 | 2^9 | 36/32 B | 32 | 5.0 KB | 3.0 KB | 7.5 KB |
| (80,9) | 8 | 2^9 | 576/512 B | 512 | 7.0 KB | 5.0 KB | 32.0 KB |

(80,9) is the "big-k at trace scale" point: 512 leaves but the full k=9
round/verify structure — useful for tracing exactly what (200,9) does,
without the memory.

### Tier 1 — tests, CI, regnet/testnet (milliseconds to seconds)

2^13–2^21 leaves; every backend and both languages run these routinely.
(120,5) is the tier's upper edge — with the current full-index backends its
real footprint is estimated ~1 GB (20–30× over naive, per §3's ratios),
making it a plausible "large testnet" point once T2.4's pointer backend
lands.

| (n,k) | ell | N | sol (min/compact) | verify | naive | Eq-P4 | Req-P6 |
|---|---|---|---|---|---|---|---|
| (72,5) | 12 | 2^13 | 52/48 B | 32 | 104 KB | 72 KB | 172 KB |
| (96,7) | 12 | 2^13 | 208/192 B | 128 | 128 KB | 96 KB | 322 KB |
| (120,9) | 12 | 2^13 | 832/768 B | 512 | 152 KB | 120 KB | 640 KB |
| (80,4) | 16 | 2^17 | 34/32 B | 16 | 1.75 MB | 1.25 MB | 2.5 MB |
| (96,5) | 16 | 2^17 | 68/64 B | 32 | 2.0 MB | 1.5 MB | 3.5 MB |
| (128,7) | 16 | 2^17 | 272/256 B | 128 | 2.5 MB | 2.0 MB | 6.4 MB |
| (160,9) | 16 | 2^17 | 1088/1024 B | 512 | 3.0 MB | 2.5 MB | 12.0 MB |
| (120,5) | 20 | 2^21 | 84/80 B | 32 | 38 MB | 30 MB | 69 MB |

### Tier 2 — memory-hard (the top of the implementable family)

2^21–2^26 leaves. Because ell ≤ 25 caps N at 2^26, this tier is the *end*
of the family — the former TB-scale sweep rows ((168,5)+, (232,7)+,
(280,9)+) are not constructible in this implementation and have been
removed; they only ever existed as arithmetic.

| (n,k) | ell | N | sol (min/compact) | verify | naive | Eq-P4 | Req-P6 | heritage |
|---|---|---|---|---|---|---|---|---|
| (200,9) | 20 | 2^21 | 1344/1280 B | 512 | 58 MB | 50 MB (48.5 pub) | 224 MB (223 pub) | **Zcash** |
| (120,4) | 24 | 2^25 | 50/48 B | 16 | 608 MB | 480 MB | 944 MB | RK measured point: **10.5 GB real** (§1d), 17× over naive |
| (168,7) | 21 | 2^22 | 352/336 B | 128 | 100 MB | 84 MB | 258 MB | |
| (144,5) | 24 | 2^25 | 100/96 B | 32 | 704 MB | 576 MB (588 pub) | 1.3 GB | **Bitcoin Gold** |
| (192,7) | 24 | 2^25 | 400/384 B | 128 | 896 MB | 768 MB | 2.3 GB | **Zero** |
| (216,8) | 24 | 2^25 | 800/768 B | 256 | 0.97 GB | 864 MB | 3.0 GB | |
| (240,9) | 24 | 2^25 | 1600/1536 B | 512 | 1.1 GB | 960 MB | 4.0 GB | k=9 family max |
| (200,7) | 25 | 2^26 | 416/400 B | 128 | 1.8 GB | 1.6 GB | 4.7 GB | family max by leaves |

**The 1–5 GB RAM band**: by the Requihash/P6 accounting it is exactly the
five largest rows — (144,5) 1.3, (192,7) 2.3, (216,8) 3.0, (240,9) 4.0,
(200,7) 4.7 GB. By the Equihash/P4 accounting only (200,7) reaches it. By
the *full-index model* (§1c — what this repo's current backends actually
pay), the band is nearly empty: the `12·2^(k−1)` term jumps the family
from (120,5) ~620 MB straight past 5 GB — only (168,7) (~3.7 GB) lands
inside, with (200,9) and (120,4) just above (~6.2–6.3 GB) and
(144,5)/(192,7)/(240,9) at 9.3/27/99 GB, unmineable here until the pointer
backend (T2.4) collapses that term. Which column applies depends entirely
on the backend representation; T2.2 turns the model into measurements.

**(240,9)**: worth considering — it is the k=9 maximum, shares the 2^25
leaf count with (144,5)/(192,7), and its P6 figure (4.0 GB) sits inside the
1–5 GB target. Testing it is gated: after T2.4 (pointer backend, honest
footprint) and T2.2 calibration, and its prediction sits exactly at the
>4 GB approval boundary — explicit owner approval required per the T2.2
protocol.

## 5. Beyond this document

Time cost and steepness under memory reduction — PLAN T3.1, not started.
Whether this repo's solvers achieve any computed figure at large n — only
measured to (96,5), §3; T2.2 extends. Decomposing the 144–178 MB real vs.
49 MB asymptotic gap at (200,9) — PLAN T3 open investigations. Sequihash
sizing from the corpus ports: none yet — the paper's Python reference is
correctness-only (4 vectors + one executed solve), and `cs-rs` gets its
`reqbench` bench binary (timing + two-instrument memory) in T4.2.
