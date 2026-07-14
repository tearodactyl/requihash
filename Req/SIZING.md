# SIZING.md — solution size, validation cost, memory across parameters

A table of solution size, verification effort, and memory requirements for
Equihash and Requihash (this project's name for its implementation of the
regularity-repaired construction; the source paper's own artifact repo calls
it Sequihash — see §0), at k ∈ {5, 7, 9}, across a range of n from a
sub-millisecond validation case to parameters requiring hundreds of GB to
mine. Same core assumptions throughout (SPEC.md's construction); naive
full-materialization memory alongside the paper's own closed-form
index-pointer estimators for both variants — **formulas transcribed from the
paper, not measurements of running code, except §2a, which is measured.**

**Correction notice (this revision):** the first version of this table
calibrated the Equihash index-pointer column against a "~49 MB at (200,9)"
figure carried in `~/Work/ZK/ZKs/Equihash.md` and repeated in this document.
That figure was never checked against the source. It has now been checked
against the paper's own artifact repo (`tl2cents/Generalized-Birthday-Problem`,
`esitmator/regular-Equihash.ipynb`) and does not match any formula the paper
actually computes; the correct figure at (200,9) is **94 MB**
(`single_list_ip_mem_estimator`). This revision replaces the calibrated model
with the paper's exact formula, computed directly — no calibration needed, and
no more accurate-by-anchor-point-only risk. See §0 for the full correction
trail. `Equihash.md`'s "~49 MB" citation needs the same fix (flagged, not yet
applied there as of this writing).

## 0. Naming note

The paper's own artifact repo names this construction **Sequihash**
throughout (README, code, notebook headers) — the eprint listing's title
itself is "On the Regularity of the Generalized Birthday Problem" (verified
directly against `eprint.iacr.org/2025/1351`); "Sequihash" is the name used
internally by the paper's authors for their construction. This project keeps
its own established name, **Requihash**, deliberately — not a transcription
error to fix, a project naming choice, stated here only so a reader searching
upstream sources knows to search "Sequihash" or the eprint number, not
"Requihash" (which was checked directly: search results for "Sequihash" and
for the eprint number turn up nothing beyond the paper itself and its one
artifact repo — no independent implementations, no third-party citations,
no community discussion found as of 2026-07-13).

**A real, executed correctness check against the paper's own code, not just
its notebook estimators:** the paper's `GBP-solver/k_list_algorithm.py` is a
runnable Wagner k-list solver (not a KAT/test-vector table — the paper
publishes no fixed input/output pairs). Fetched and actually run this session
at a tiny parameter point (n=24, k=2^3, a fixed nonce): it found 3 solutions
and self-verified one (`hashval == 0` XOR check passed). This confirms the
solver code is real and runs, and gives one genuinely executed reference
point — it does not, by itself, validate any of the memory/time formulas
below, which are separate closed-form estimators in a different file
(`esitmator/regular-Equihash.ipynb`), not derived from running this solver.

## 1. Method and evidence grades

Six figures per row, each with a distinct evidence grade — stated per column,
not implied:

| Column | Formula | Grade |
|---|---|---|
| `ell` (collision bit length) | `n / (k+1)` | Definitional |
| Init list size `N` | `2^(ell+1)` | Definitional |
| Solution size (minimal / compact) | `2^k·(ell+1)/8` and `2^k·ell/8` bytes | **Measured** — computed by `Params::solution_width`/`compact_width` in `rust/src/lib.rs`, exercised by the `table3_wire_sizes` test |
| Verify hashes (m=1) | `2^k` | Definitional; the `m` multiplier (SPEC.md §5–6) scales this linearly and is orthogonal to the table below |
| Naive peak memory | `N · (n/8 + 4)` bytes | **Formula only, and now known to be WRONG by 20–50×** — this was called "measured shape" in the prior revision. It is not. §2a below reports an actual counting-allocator measurement of this repo's real `solve_reference`/`solve_arena` at four parameter points: the true peak exceeds this formula by 20–52×, because per-row `Vec<u8>`/`Vec<u32>` heap allocation overhead (allocator bookkeeping, capacity rounding — exactly the cost BENCHMARK.md's time-profiling already found dominant) is not modeled at all. The formula only counts raw payload bytes. Every "naive peak mem" figure in the table below (§2) inherits this same underestimate and should be read as a payload-only lower bound, not a memory requirement |
| Index-pointer peak, Equihash | `2·(n + k − ell − 1) · N` bits — the paper's `single_list_ip_mem_estimator`, evaluated exactly (fetched and run against `esitmator/regular-Equihash.ipynb` in the paper's own artifact repo, 2026-07-13) | **Formula only, unverified against any running code.** No index-pointer solver exists in this repo (PLAN.md A6) or has been run by this project. This is a transcription of the paper's own asymptotic estimator, correctly reproducing what the notebook computes — it is not evidence that any real implementation actually achieves this figure. Given §2a's finding that a much simpler naive formula was wrong by 20–50×, this asymptotic estimator should not be assumed accurate either until checked against a real index-pointer implementation (tromp's `equi_miner.h`, upstream — not the stripped single-thread crate port; see PLAN.md A6/A15) |
| Index-pointer peak, Sequihash/Requihash | `((k²+5k+2)/4·ell + 2^(k−1)) · N` bits — the paper's `best_k_list_memory_trade_off` at its optimal trim `t=1`, which is what the paper's own Table 3 (`M_sgbp`) actually reports, verified by replicating the notebook's computation directly | **Formula only, unverified against any running code** — same caveat as the row above. This repo has not implemented and run this variant either |

**What this table is not:** a benchmark. No wall-clock time appears. It is a
memory/size *sizing* reference — how big each artifact gets — separate from
the throughput numbers in BENCHMARK.md. It is also not a claim that either
index-pointer solver exists in this repo — neither does (PLAN.md A6, not
started); both index-pointer columns are the paper's own asymptotic
estimators, evaluated exactly, not measurements of running code.

## 2a. Actually measured: real peak allocation vs. the naive formula

Built and run this session: `rust/src/bin/req_memcheck.rs`, a global
counting allocator wrapped around this repo's own `solve_reference` and
`solve_arena`, executed at real parameters (`cargo run --release --bin
req_memcheck`). This is the one row of this document backed by executed
code rather than a transcribed or derived formula.

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
runtime. The two findings corroborate each other via different
instruments, which is exactly the kind of cross-check this document should
have had from the start. **This measurement does not extend past (96,5)**
— (144,5) and beyond were not run this session (the naive Python k-list
solver used for the earlier real-code check, run separately above, already
demonstrated how slow an unoptimized solve gets; this repo's own Rust
solvers are much faster but were not pushed to (144,5) in this pass). Every
figure at (144,5) and larger in §2's table remains pure arithmetic,
unverified against any execution, naive or otherwise.

## 2. The table

k = 5, 7, 9; n swept from a trivial validation case to parameters whose naive
peak memory reaches multi-TB (the "hundreds of GB" case sits mid-table for
each k; going further is definitional continuation, not a new regime).

**All memory figures below are formula arithmetic, not measurements**, except
where they overlap the four rows actually measured in §2a — and §2a already
showed the naive formula underestimates real peak memory by 20–52× at every
point it checked. Treat every "naive peak mem" entry below as a payload-only
floor, not an expected real requirement; treat both index-pointer columns as
unverified paper transcriptions (§1's grade column). Do not read the table
below as validated.

| k | n | ell | init list N | sol size (min/compact) | verify hashes (m=1) | naive peak mem | index-pointer peak, Equihash (exact) | index-pointer peak, Sequihash/Requihash (exact) |
|---|---|---|---|---|---|---|---|---|
| 5 | 24 | 4 | 2^5 | 20/16 B | 32 | 224 B | 192 B | 272 B |
| 5 | 48 | 8 | 2^9 | 36/32 B | 32 | 5.0 KB | 5.5 KB | 7.5 KB |
| 5 | 96 | 16 | 2^17 | 68/64 B | 32 | 2.0 MB | 2.6 MB | 3.5 MB |
| 5 | 144 | 24 | 2^25 | 100/96 B | 32 | 704 MB | 992 MB | 1.28 GB |
| 5 | 168 | 28 | 2^29 | 116/112 B | 32 | 12.5 GB | 18.0 GB | 23.8 GB |
| 5 | 192 | 32 | 2^33 | 132/128 B | 32 | 224 GB | 328 GB | 432 GB |
| 5 | 216 | 36 | 2^37 | 148/144 B | 32 | 3.9 TB | 5.75 TB | 7.6 TB |
| 7 | 32 | 4 | 2^5 | 80/64 B | 128 | 256 B | 272 B | 600 B |
| 7 | 96 | 12 | 2^13 | 208/192 B | 128 | 128 KB | 180 KB | 322 KB |
| 7 | 168 | 21 | 2^22 | 352/336 B | 128 | 100 MB | 153 MB | 258 MB |
| 7 | 200 | 25 | 2^26 | 416/400 B | 128 | 1.81 GB | 2.83 GB | 4.70 GB |
| 7 | 232 | 29 | 2^30 | 480/464 B | 128 | 33.0 GB | 52.3 GB | 85.9 GB |
| 7 | 264 | 33 | 2^34 | 544/528 B | 128 | 592 GB | 948 GB | 1.51 TB |
| 7 | 296 | 37 | 2^38 | 608/592 B | 128 | 10.25 TB | 16.56 TB | 26.9 TB |
| 9 | 40 | 4 | 2^5 | 320/256 B | 512 | 288 B | 352 B | 1.5 KB |
| 9 | 120 | 12 | 2^13 | 832/768 B | 512 | 152 KB | 232 KB | 640 KB |
| 9 | 200 | 20 | 2^21 | 1344/1280 B | 512 | 58.0 MB | **94.0 MB** | 224 MB |
| 9 | 240 | 24 | 2^25 | 1600/1536 B | 512 | 1.06 GB | 1.75 GB | 4.0 GB |
| 9 | 280 | 28 | 2^29 | 1856/1792 B | 512 | 19.5 GB | 32.5 GB | 72.0 GB |
| 9 | 320 | 32 | 2^33 | 2112/2048 B | 512 | 352 GB | 592 GB | 1.25 TB |
| 9 | 360 | 36 | 2^37 | 2368/2304 B | 512 | 6.12 TB | 10.4 TB | 22.0 TB |

The (200,9) row is bold in the Equihash column: 94.0 MB is the corrected,
exact figure (was wrongly given as 49.0 MB, calibrated, in the prior
revision). The Sequihash/Requihash column's 224 MB independently reproduces
the paper's cited Table 3 figure at (200,9).

## 3. Readings

- **Solution size and verify cost are k-only, n-independent in count** (32,
  128, 512 hashes for k=5,7,9 respectively) but grow linearly in bytes with n
  — the (n, k) split is exactly the "verification cheapness vs. memory
  hardness" dial the algorithm exposes, visible directly in column 4 vs. 6.
- **The index-pointer trick does not reduce memory below the naive figure at
  most points in this sweep — the opposite of this table's prior claim.**
  With the corrected exact formulas, the Equihash index-pointer estimator is
  *larger* than this table's naive figure everywhere except the very smallest
  parameters (k=5/n=24 and k=7/n=32 are the only two rows where index-pointer
  wins, both trivially small). This is not a contradiction of the historical
  record — it reflects that the paper's `single_list_ip_mem_estimator` and
  this table's "naive" column are not modeling the same baseline: the paper's
  formula already assumes truncated indices and is optimizing for asymptotic
  behavior at the parameter regime index pointers were designed for (large k,
  where per-node index-list overhead dominates), not for a head-to-head at
  every (n,k) point on this sweep. **This is flagged, not resolved** — it
  means either this table's "naive" formula or the paper's index-pointer
  formula (or both) needs recalibration against a real implementation before
  either column can be trusted as a genuine comparison. The safest current
  reading: only the Sequihash-vs-Equihash *ratio* (next bullet), computed from
  the same two paper formulas on the same basis, is likely reliable; neither
  index-pointer column should be compared against the "naive" column without
  further verification.
- **Sequihash/Requihash costs 1.3–2.4× the Equihash index-pointer memory at
  matched (n,k)**, and the ratio grows with k (k=5/n=144: 1.32×; k=7/n=200:
  1.66×; k=9/n=200: 2.38×) — smaller than this table's prior (incorrect)
  claim of "2–5×, growing to 4.6×," but the qualitative finding survives:
  regularity costs more memory, and progressively more so as k grows, which
  the `(k²+5k+2)/4` term (quadratic in k) predicts against the Equihash
  formula's k-linear term.
- **The "hundreds of GB" case** lands at k=5/n=192, k=7/n=264, k=9/n=320 —
  each a legitimate n%(k+1)==0, n%8==0 parameter choice, none deployed by any
  known chain (the Equihash-family ecosystem survey in `~/Work/ZK/ZKs/Equihash.md`
  §4 tops out at (200,9) and (144,5); nothing in that table approaches this
  memory class).

## 4. What this table does not answer

Time cost (steepness under memory reduction — PLAN.md A5, not started);
whether the "naive" solvers in this repo actually *achieve* the modeled peak
at large n (only measured up to (96,5)/(144,5) per BENCHMARK.md; the TB-scale
rows are pure arithmetic, unexercised); and — now the more pressing gap — why
the two "exact" formulas in this table disagree so sharply with the informal
"index pointers roughly halve memory" intuition the historical record (§9 of
`Equihash.md`) reports for real solvers (tromp: ~144 MB at (200,9), stated in
`equi_miner.c`'s own source comment to already assume a `SAVEMEM = 9/14`
bucket-sizing tradeoff "with negligible discarding"; xenoncat: 178 MB fixed
context, a different implementation with its own layout). None of the three
numbers here (this table's naive 58 MB, this table's formula-derived 94 MB,
and tromp's real ~144 MB) agree on a single consistent basis. One numeric
curiosity, flagged as *unconfirmed and likely coincidental* rather than a
finding: undoing tromp's stated 9/14 SAVEMEM factor (144 ÷ 9/14) lands almost
exactly on 224 MB — this table's *Sequihash/Requihash* figure, not its
Equihash one. No algebraic connection between tromp's bucket-sizing tradeoff
and the paper's k-list memory formula has been shown; this is likely
arithmetic accident, not evidence of anything, and is recorded only so a
future pass doesn't rediscover it and over-read it. Reconciling what tromp's
144 MB actually measures (peak resident set of the real C program, including
bucket-table padding neither formula in this table models) against the two
closed-form estimators is the immediate next-highest-priority correction,
ahead of extending this table further — tracked in PLAN.md as a new item.
