# SIZING.md — solution size, validation cost, memory across parameters

A table of solution size, verification effort, and memory requirements for
Equihash and Requihash (this project's name for its implementation of the
regularity-repaired construction; the source paper's own artifact repo calls
it Sequihash — see §0), at k ∈ {5, 7, 9}, across a range of n from a
sub-millisecond validation case to parameters requiring hundreds of GB to
mine. Same core assumptions throughout (SPEC.md's construction); naive
full-materialization memory alongside the paper's own published Table 3
figures for both variants — **the published-table figures are transcribed
from the paper's PDF directly, not derived from any formula; the two
"index-pointer" columns' provenance is stated precisely per-column in §1,
because this document previously got that provenance wrong once already
(§0a) and a second silent error is not acceptable.**

## 0. Naming note

The paper (Lili Tang, Yao Sun, Xiaorui Gong, [eprint 2025/1351](https://eprint.iacr.org/2025/1351),
"On the Regularity of the Generalized Birthday Problem" — full citation and
three companion 2025 papers by overlapping authors: `~/Work/ZK/Requihash/PAPERS.md`)
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
input/output pairs anywhere. Fetched and actually run this session at a tiny
parameter point (n=24, k=2^3, a fixed nonce): it found 3 solutions and
self-verified one (`hashval == 0` XOR check passed). This confirms the
solver code is real and runs, and gives one genuinely executed reference
point — it does not, by itself, validate any of the memory/time figures
below, which come from the paper's *published tables*, a separate artifact.

## 0a. Correction trail — read this before trusting any number below

This document's memory figures were wrong once already, and the error is
recorded here in full rather than quietly fixed, because the *mechanism* of
the error is itself the lesson:

1. **First pass** (session of 2026-07-12/13): built a "naive vs. index-pointer"
   table calibrating a linear model against a "~49 MB at (200,9)" figure
   already sitting in `~/Work/ZK/ZKs/EquihashSurvey.md`. That 49 MB figure was
   itself never checked against the paper — it had been carried in this
   project's documents from an earlier session, ultimately traceable to the
   paper's own published Table 3, but nobody had opened the PDF to confirm.
2. **Second pass** (2026-07-13): flagged the 49 MB figure as suspicious and
   "corrected" it to **94 MB**, reasoning from the paper's companion Jupyter
   notebook (`esitmator/regular-Equihash.ipynb`, function
   `single_list_ip_mem_estimator`). This was **itself wrong** — that notebook
   function computes a different, more conservative estimate than what the
   paper actually published in its own Table 3. The "correction" replaced a
   right answer with a wrong one, because a formula in a companion artifact
   was trusted over the paper's own printed table.
3. **This pass** (2026-07-14): obtained the actual PDF (`~/Downloads/2025-1351.pdf`)
   and read Table 3 on page 31 directly. **The published table says 2^28.6
   bits ≈ 49 MB for Equihash(200,2^9) and 2^30.8 bits ≈ 223 MB for
   Sequihash(200,2^9).** The original 49 MB was correct all along.
   Root-caused precisely: Table 3's Equihash memory column matches
   Proposition 4's plain `O(n·N)` bound with constant ≈1 (computed directly:
   `n·N` at (200,9) = 2^28.64 bits, matching the table's 2^28.6 almost
   exactly); it does *not* match the appendix's Proposition 7, which derives
   a *different* index-pointer bound (`2·(n+k−ℓ−1)·N`, giving 94 MB) for the
   same parameters. **The paper contains two different single-list memory
   estimates in two different places, and they do not agree with each
   other** — Table 3 in the main text (§5.2) uses the simpler Proposition-4
   bound; the appendix's Proposition 7 gives a more detailed derivation that
   happens to come out larger. Neither is "the wrong one" in an absolute
   sense — they're bounding different things (a loose big-O headline vs. a
   worked-through peak-memory derivation) — but citing one while believing
   it's the other is the exact error made twice in this document's history.

**The standing rule going forward:** any figure attributed to this paper
must cite which specific equation/table/page it came from, not just "the
paper's estimator" — because the paper itself is not internally consistent
across its own artifacts, and a citation without a locator cannot be checked
by the next person (including a future instance of this project).

## 1. Method and evidence grades

Six figures per row, each with a distinct evidence grade — stated per
column, not implied:

| Column | Formula / source | Grade |
|---|---|---|
| `ell` (collision bit length) | `n / (k+1)` | Definitional |
| Init list size `N` | `2^(ell+1)` | Definitional |
| Solution size (minimal / compact) | `2^k·(ell+1)/8` and `2^k·ell/8` bytes | **Measured** — computed by `Params::solution_width`/`compact_width` in `rust/src/lib.rs`, exercised by the `table3_wire_sizes` test; also matches the paper's own published Table 3 solution-size column exactly at every row checked |
| Verify hashes (m=1) | `2^k` | Definitional; the `m` multiplier (SPEC.md §5–6) scales this linearly and is orthogonal to the table below |
| Naive peak memory | `N · (n/8 + 4)` bytes | **Formula only, and known to be WRONG by 20–52× against this repo's own real code** — see §2a: a counting-allocator measurement of `solve_reference`/`solve_arena` shows the true peak far exceeds this payload-only formula, because per-row `Vec<u8>`/`Vec<u32>` heap allocation overhead is not modeled at all. Every "naive peak mem" figure in §2's table inherits this same underestimate |
| Equihash memory (published) | Paper's own **Table 3** (page 31), transcribed directly from the PDF at the seven (n,k) points the paper lists; **not** independently computed by this project for those rows. Extrapolated to other (n,k) in the sweep below using Proposition 4's `O(n·N)` bound at constant 1, which exactly reproduces the paper's own Table 3 value at every point checked (verified: (200,9) → 2^28.64 computed vs. 2^28.6 published) | **Published-table where the paper lists it; formula-extrapolated (Proposition 4, constant 1) elsewhere, extrapolation validated against every published point it can be checked against** |
| Requihash/Sequihash memory (published) | Paper's own **Table 3**, same treatment; extrapolated elsewhere via **Proposition 6** (`(k²+5k+2)/4·ell + 2^(k−1)) · N` bits — the k-list-with-index-trimming bound, which is the formula that actually reproduces the paper's Table-3 Sequihash column, confirmed at (200,9): computes 2^30.8, matching the table exactly) | **Published-table where listed; formula-extrapolated (Proposition 6) elsewhere, validated the same way** |

**A distinct, separate, and unverified formula exists in the same paper**
(Proposition 7, appendix D.2: `2·(n+k−ℓ−1)·N` bits) that computes 94 MB at
(200,9) — larger than Table 3's 49 MB for the same nominal parameters. This
document does **not** use Proposition 7 for its Equihash column, having
learned the hard way (§0a) that doing so silently contradicts the paper's
own headline table. Proposition 7 is recorded here, not deleted, because it
is a real formula in a real paper and a future reader should be able to find
it and understand why it isn't the one used: it appears to bound a
different, more pessimistic implementation strategy than whatever produced
Table 3, and the paper does not reconcile the two anywhere this project has
found.

**What this table is not:** a benchmark. No wall-clock time appears. It is
a memory/size *sizing* reference — how big each artifact gets — separate
from the throughput numbers in BENCHMARK.md. It is also not a claim that
either index-pointer solver exists in this repo — neither does (PLAN.md A6,
not started); the memory columns are the paper's own published/extrapolated
figures, not measurements of running code in this repo.

## 2a. Actually measured: real peak allocation vs. the naive formula

Built and run this session: `rust/src/bin/req_memcheck.rs`, a global
counting allocator wrapped around this repo's own `solve_reference` and
`solve_arena`, executed at real parameters (`cargo run --release --bin
req_memcheck`). This is the one section of this document backed by executed
code rather than a transcribed or extrapolated formula.

| (n,k) | formula (§1's naive model) | measured peak, `solve_reference` | measured peak, `solve_arena` | ratio (reference) |
|---|---|---|---|---|
| (24,5) | 224 B | 11.4 KB | 8.6 KB | 52.0× |
| (48,5) | 5.0 KB | 174.7 KB | 164.8 KB | 34.9× |
| (72,5) | 104.0 KB | 2.00 MB | 2.07 MB | 19.7× |
| (96,5) | 2.00 MB | 55.4 MB | 66.0 MB | 27.7× |

At (96,5) this works out to ~443 measured bytes per row against the
formula's assumed 16 — a ~28× per-row overhead, consistent with
BENCHMARK.md's independent finding (via time profiling, not memory
profiling) that per-row heap allocation is ~59% of `solve_reference`'s
runtime. The two findings corroborate each other via different instruments.
**This measurement does not extend past (96,5)** — (144,5) and beyond were
not run this session. Every figure at (144,5) and larger in §2's table
remains pure arithmetic (paper-published or paper-extrapolated), unverified
against any execution of this project's own code.

## 2. The table

k = 5, 7, 9; n swept from a trivial validation case to parameters whose
naive peak memory reaches multi-TB, filled out to cover every deployed or
paper-cited (n,k) pair in this range (Zero Currency's (192,7)
[Zero400/ZERO_COIN.md], Zcash's (200,9), Bitcoin Gold's (144,5)) alongside
the even geometric spacing. **Every row below is this project's own
extrapolation** via Proposition 4 (Equihash) / Proposition 6 (Requihash) at
constant 1, computed fresh for this table. One row, (200,9), coincides
exactly with a paper Table 3 point (§2b) and is included here anyway for a
consistent sweep — this project's independently computed extrapolation at
that point matches the published Table 3 value to within ±0.04 in log2
bits (see §1), so the two are cross-checks of each other, not
duplicates in provenance. Both formulas were validated against **all
seven** of the paper's own published Table 3 rows (not just one point)
before use here — every prediction matched the published value to within
±0.04 in log2 bits, consistent with the paper's one-decimal rounding. Full
validation arithmetic: `Req/SIZING.md` git history / this session's working
notes.

**Note on `scripts/equihash_formulas.py`**: this table's underlying
formulas are also implemented there as a reusable script (`--csv`/
`--validate` modes), but its `SWEEP_POINTS` currently reproduce an older,
smaller version of the sweep below, not this table as it stands —
flagged as out of sync in `Req/PLAN.md` A20, not yet fixed. Do not treat
the script's own default output as authoritative until that's resolved;
this table (below) is the current source of truth.

| k | n | ell | init list N | sol size (min/compact) | verify hashes (m=1) | naive peak mem (this repo's formula) | Equihash memory (extrapolated) | Requihash memory (extrapolated) |
|---|---|---|---|---|---|---|---|---|
| 5 | 24 | 4 | 2^5 | 20/16 B | 32 | 224 B | 96 B | 272 B |
| 5 | 48 | 8 | 2^9 | 36/32 B | 32 | 5.0 KB | 3.0 KB | 7.5 KB |
| 5 | 72 | 12 | 2^13 | 52/48 B | 32 | 104.0 KB | 72.0 KB | 172.0 KB |
| 5 | 96 | 16 | 2^17 | 68/64 B | 32 | 2.0 MB | 1.5 MB | 3.5 MB |
| 5 | 120 | 20 | 2^21 | 84/80 B | 32 | 38.0 MB | 30.0 MB | 69.0 MB |
| 5 | 144 | 24 | 2^25 | 100/96 B | 32 | 704.0 MB | 576.0 MB | 1.3 GB |
| 5 | 168 | 28 | 2^29 | 116/112 B | 32 | 12.5 GB | 10.5 GB | 23.8 GB |
| 5 | 192 | 32 | 2^33 | 132/128 B | 32 | 224.0 GB | 192.0 GB | 432.0 GB |
| 5 | 216 | 36 | 2^37 | 148/144 B | 32 | 3.9 TB | 3.4 TB | 7.6 TB |
| 7 | 32 | 4 | 2^5 | 80/64 B | 128 | 256 B | 128 B | 600 B |
| 7 | 96 | 12 | 2^13 | 208/192 B | 128 | 128.0 KB | 96.0 KB | 322.0 KB |
| 7 | 128 | 16 | 2^17 | 272/256 B | 128 | 2.5 MB | 2.0 MB | 6.4 MB |
| 7 | 168 | 21 | 2^22 | 352/336 B | 128 | 100.0 MB | 84.0 MB | 257.8 MB |
| 7 | 192 | 24 | 2^25 | 400/384 B | 128 | 896.0 MB | 768.0 MB | 2.3 GB |
| 7 | 232 | 29 | 2^30 | 480/464 B | 128 | 33.0 GB | 29.0 GB | 85.9 GB |
| 7 | 264 | 33 | 2^34 | 544/528 B | 128 | 592.0 GB | 528.0 GB | 1.5 TB |
| 7 | 296 | 37 | 2^38 | 608/592 B | 128 | 10.2 TB | 9.2 TB | 26.9 TB |
| 9 | 40 | 4 | 2^5 | 320/256 B | 512 | 288 B | 160 B | 1.5 KB |
| 9 | 120 | 12 | 2^13 | 832/768 B | 512 | 152.0 KB | 120.0 KB | 640.0 KB |
| 9 | 160 | 16 | 2^17 | 1088/1024 B | 512 | 3.0 MB | 2.5 MB | 12.0 MB |
| 9 | 200 | 20 | 2^21 | 1344/1280 B | 512 | 58.0 MB | 50.0 MB | 224.0 MB |
| 9 | 240 | 24 | 2^25 | 1600/1536 B | 512 | 1.1 GB | 960.0 MB | 4.0 GB |
| 9 | 280 | 28 | 2^29 | 1856/1792 B | 512 | 19.5 GB | 17.5 GB | 72.0 GB |
| 9 | 320 | 32 | 2^33 | 2112/2048 B | 512 | 352.0 GB | 320.0 GB | 1.2 TB |

### 2b. The paper's own published Table 3, verbatim (page 31 of the PDF)

Given directly, no extrapolation, for comparison against the sweep above —
this is what the correction trail in §0a is about, and it is worth having
in full rather than cherry-picked:

| (n, K) | Equihash Time | Equihash Mem. | Equihash Sol-Size | Sequihash Time | Sequihash Mem. | Sequihash Sol-Size |
|---|---|---|---|---|---|---|
| (96, 2^5) | 2^19.3 | 2^23.6 | 68 B | 2^22.6 | 2^24.8 | 64 B |
| (128, 2^7) | 2^19.8 | 2^24.0 | 272 B | 2^24.6 | 2^25.7 | 256 B |
| (160, 2^9) | 2^20.2 | 2^24.3 | 1088 B | 2^26.6 | 2^26.6 | 1024 B |
| (144, 2^5) | 2^27.3 | 2^32.2 | 100 B | 2^30.6 | 2^33.4 | 96 B |
| (150, 2^5) | 2^28.3 | 2^33.2 | 104 B | 2^31.6 | 2^34.4 | 100 B |
| (200, 2^9) | 2^24.2 | 2^28.6 | 1344 B | 2^30.6 | 2^30.8 | 1280 B |
| (288, 2^8) | 2^36.0 | 2^41.2 | 1056 B | 2^41.6 | 2^42.9 | 1024 B |

In MB/GB (bits ÷ 8, verified by direct computation — memory columns in Table
3 are bit counts, per the paper's own convention stated in §1 above): (96,5)
2^23.6 ≈ **1.5 MB** / 2^24.8 ≈ **3.5 MB**; (128,7) 2^24.0 = **2.0 MB** /
2^25.7 ≈ **6.5 MB**; (160,9) 2^24.3 ≈ **2.5 MB** / 2^26.6 ≈ **12.1 MB**;
(144,5) 2^32.2 ≈ **588 MB** / 2^33.4 ≈ **1.3 GB**; (150,5) 2^33.2 ≈
**1.1 GB** / 2^34.4 ≈ **2.6 GB**; (200,9) 2^28.6 ≈ **48.5 MB** / 2^30.8 ≈
**223 MB**; (288,8) 2^41.2 ≈ **294 GB** / 2^42.9 ≈ **955 GB**. (The
narrative text on page 31 rounds (200,9) to "49 MB"/"223 MB" — consistent
with 48.5/223 here to the precision the paper itself uses.)

## 3. Readings

- **Solution size and verify cost are k-only, n-independent in count** (32,
  128, 512 hashes for k=5,7,9 respectively) but grow linearly in bytes with
  n — the (n, k) split is exactly the "verification cheapness vs. memory
  hardness" dial the algorithm exposes.
- **Requihash costs roughly 2.3–4.9× the Equihash memory at matched (n,k)**,
  computed directly from the paper's own published Table 3 ratios (§2b), not
  an extrapolation: (96,5) and (144,5) both give **2.30×**; (128,7) and
  (288,8) both give **3.25×**; (160,9) gives **4.92×**; (200,9) gives
  **4.59×** (the paper's own stated round number is "a factor of 4.6" — this
  document's arithmetic matches it exactly). The pattern across k is
  suggestive (k=5 rows cluster near 2.3×, k=9 rows near 4.6–4.9×, k=7 rows
  near 3.25×) but is not a clean single-variable function of k alone since
  it also depends on n within each k — treat "the ratio grows with k" as
  directionally supported, not a precise closed form, without deriving one
  from just two or three points. This document's earlier (now-corrected)
  claim of "1.3–2.4×, growing with k" used the wrong Equihash-side formula
  (Proposition 7, 94 MB at (200,9) instead of the published 49 MB) and is
  retired along with that figure.
- **The index-pointer trick barely beats this repo's own "naive" formula at
  (200,9)** — 58.0 MB (naive) vs. 48.5 MB (published Equihash) is only a
  **~1.2×** difference, not the large win intuition might suggest. This is
  almost certainly because this repo's "naive peak memory" formula (§1) is
  *already* a fairly compact model (raw payload only, no allocator
  overhead — confirmed too low by 20–52× against real measured code, §2a),
  so it isn't a fair "no optimization at all" baseline to begin with; a
  genuinely naive full-index-storage implementation would need a different,
  larger formula (e.g. storing full `2^k`-length index vectors per row
  rather than the truncated/compact representation this repo's formula
  already assumes) to show the historically-cited index-pointer win
  properly. That larger, truly-naive formula has not been built or
  validated in this document — flagged as unresolved, not asserted.

## 4. A fourth data point: 2025/2141's own claimed implementation numbers

Distinct from everything above — this is neither this document's model nor
1351's asymptotic Table 3, but a **third paper's own claimed implementation
figures** (Tang, Ding, Sun, Gong, [eprint 2025/2141](https://eprint.iacr.org/2025/2141),
"Memory Optimizations of Wagner's Algorithm with Applications to Equihash" —
overlapping authors with 1351; full citation `~/Work/ZK/Requihash/PAPERS.md`).
Quoted directly from the abstract and body text (page 5): **"For Equihash(144,5),
our optimized algorithm requires only 700 MB of memory, compared to
approximately 2.5 GB in previous implementations"**; body text: *"Compared to
state-of-the-art software implementations in [Tromp], our baseline
implementation for Equihash(144,5) achieves nearly the same runtime, while
requiring only 1.45 GB of memory (0.57× of Tromp's 2.5 GB footprint).
Moreover, by accepting roughly a 2× time penalty factor, the peak memory can
be further reduced to 700 MB (0.28× of Tromp's)."*

This is a genuinely different kind of number from everything else in this
document: **it is the paper's own claim about its own implementation's
measured behavior**, attributed by name to "Tromp" as the baseline being
improved on (i.e. this paper claims Tromp's own (144,5) implementation uses
~2.5 GB) — not a Wagner-framework asymptotic estimate. It has not been
independently verified by this project (their code is at
`github.com/tl2cents/Wagner-Algorithms`, not yet cloned or run here) and
should not be conflated with 1351's Table 3 figures above, which are for a
different parameter regime's asymptotic model, not a measured software
footprint. Recorded here as a fourth, clearly-labeled reference point, not
merged into the table.

## 5. What this table does not answer

Time cost (steepness under memory reduction — PLAN.md A5, not started);
whether this repo's own naive solvers actually *achieve* either the
formula's or the paper's peak at large n (only measured up to (96,5) per
§2a; the TB-scale rows are pure arithmetic, unexercised); and reconciling
tromp's real measured ~144 MB at (200,9) (`equi_miner.c`'s own comment: a
`SAVEMEM = 9/14` bucket-sizing tradeoff "with negligible discarding" applied
on top of some unstated baseline) against the paper's 49 MB Table-3 figure
for the same nominal parameters — these do not obviously agree either (144
vs. 49 MB is a ~3× gap). **Partially narrowed** (SOLVERS.md §0.3, this
session): tromp's own README states this 144MB figure directly as his real
solver's measured (200,9) footprint, in the same breath as xenoncat's real
178MB for the same parameters — both are genuine author-stated
implementation numbers for different bucket-sort/pair-compression design
choices, not a mysterious pair to reconcile against each other. What
*remains* open is only reconciling either real number against the paper's
49 MB **asymptotic** Proposition 4 bound — a different kind of gap (real
implementation overhead vs. big-O estimate), tracked in PLAN.md A12. The
earlier "144 ÷ 9/14 ≈ 224 MB" numeric observation from this document's
prior revision is **retracted along with the 94 MB figure it was compared
against** — it was coincidental arithmetic built on an error and should not
be re-derived or over-read in any future pass.
