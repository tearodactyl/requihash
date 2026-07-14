# PLAN.md — Groups A–C: sequence, status, and requirements

The organizing fact (BENCHMARK.md §8, "Segregation from node development"):
two independent tracks, coupled only by SPEC.md and the vector-file format.

- **Group A — PoW lab** (this repo, `Requihash/Req`): hash/parameter/algorithm
  research. Self-contained; no Zebro dependency.
- **Group B — node track** (`Zebro/`): identity, genesis, and the
  entanglement-scorecard tooling that sizes future forks. Owned by ZEBRO.md
  §1's task list; summarized here only to keep the cross-track view in one
  place.
- **Group C — bridge**: items that touch both tracks or external parties.

Status legend: **Done** (merged/measured, artifact cited) · **In progress**
(work started, not converged) · **Not started** (designed, zero code/query).

## Group A — PoW lab

| Step | Work | Status | Artifact |
|---|---|---|---|
| A1 | Family specification: byte-exact `PoW(n,k,hash,m,keying,context)` | **Done** | `SPEC.md` |
| A2 | Regression discipline: JSONL records, min+median+MAD, baseline comparison, decision band | **Done** | `rust/src/report.rs`, `baselines/` |
| A3 | BLAKE3 backend + `Iterated` semantics + substitution benches | **Done** | `rust/src/probe.rs` (`GenProbe`, `Variant`), `SPEC.md` §5–6 |
| A4 | First campaign: blake2b vs blake3 × m at (96,5)/(144,5)/(200,9) | **Done** (ARM only) | `BENCHMARK.md` §9 |
| A4b | Implementation-matched rerun: `blake2b_simd` backend + equivalence gate | **Done** (ARM only) | `rust/src/probe.rs` (`HashKind::Blake2bSimd`), `BENCHMARK.md` §9 |
| A5 | Counting harness + memory-capped solver → TMTO steepness sweep | **Not started** | Design only: `TMTO.md` §3, §3a |
| A6 | Compact index-pointer solver backend (unlocks (200,9) composite solves) | **Not started** | Flagged as off the D3 critical path (BENCHMARK.md §8 gap 5). **A real reference implementation for this now exists locally, unexploited**: `tromp/equi_miner.c`, vendored inside the pinned `equihash` crate at `~/.cargo/registry/src/*/equihash-0.3.0/tromp/` — the exact bit-packed `tree`/bucket index-pointer structure this backend needs, plus the crate's own `test_vectors/valid.rs`/`invalid.rs` (official Zcash Equihash(96,5)/(200,9) KATs) for correctness-testing a ported version |
| A7 | x86-64/AVX2 leg of A4b | **Deprioritized** | No artifact; dropped per direct instruction. Re-open only if an x86 box becomes available and the AVX2 delta becomes decision-relevant |
| A8 | Solution/memory sizing table, naive vs. index-pointer, Equihash vs. Requihash, k∈{5,7,9} | **Done** | `SIZING.md` |
| A9 | Redirected focus: implementation quality, concurrency, data-sizing fitness (replaces further hash-vs-hash comparison as the primary lab activity) | **In progress** — this document's own next section | Below |
| A10 (new) | Naming note (not a correction — project keeps "Requihash" by explicit decision): the source paper's own artifact repo calls the construction **Sequihash** — verified directly against `tl2cents/Generalized-Birthday-Problem`. Also verified: searching "Sequihash" or the eprint number turns up nothing beyond the paper and that one repo — no independent implementations or citations found | **Done, no rename** | `SIZING.md` §0 and `Equihash.md` both mention the paper's naming inline, once, when citing the paper directly; "Requihash" remains this project's name everywhere else |
| A11 (new) | Memory-formula correction: `Equihash.md`'s and the prior `SIZING.md`'s "~49 MB at (200,9)" figure for Equihash index-pointer memory does not match the paper's own estimator (`single_list_ip_mem_estimator` gives 94 MB) — the figure was carried across documents without ever being checked against the source | **Done** — fixed in both `SIZING.md` (exact formula, no calibration) and `~/Work/ZK/ZKs/Equihash.md` (94 → 224 MB, with the reconciliation caveat inline) |
| A12 (new) | Reconciliation gap: neither the naive nor the exact index-pointer formula in `SIZING.md` matches tromp's real measured ~144 MB at (200,9) — three inconsistent numbers on the same nominal parameters | **Not started** — see `SIZING.md` §4 for the specifics; likely requires actually reading/running `equi_miner.c`'s bucket-sizing arithmetic rather than trusting either closed-form estimator. Numeric curiosity noted there (144 ÷ 9/14 ≈ 224 MB matching the Sequihash figure) but explicitly flagged as unconfirmed, likely-coincidental — not a finding |
| A13 (new) | BLAKE2b NEON: the official `BLAKE2/BLAKE2` reference repo ships a maintained `neon/` directory (2018, correctness-patched 2023) — real, usable, aarch64-targeted C code, unlike `blake2b_simd` (Rust crate, x86-only). ZeroPerf's `Perf.md` §9.2 already scoped vendoring it for the C++/libsodium path in detail (vendor-don't-link, standalone KAT harness, feature-flag gate, differential test). Real-world caveat found via web search, not yet verified locally: at least one report of BLAKE2b NEON running *slower* than scalar on ARMv8/Cortex-A57 — do not assume NEON is a win here without measuring | **Not started here** — the ZeroPerf plan is a template this repo could reuse: add a fourth `HashKind`/`LeafHasher` backend wrapping vendored `blake2b-neon.c` via a small FFI shim, gated behind a new feature, equivalence-gated exactly like `Blake2bSimd` was, **then actually measured on this machine** before any claim about it helping or hurting |
| A14 (new) | Test-vector source for a ported index-pointer solver (A6): the pinned `equihash` crate (already a Zebro dependency) vendors official Zcash Equihash(96,5)/(200,9) known-answer vectors at `src/test_vectors/valid.rs`/`invalid.rs`, sitting unused in the local Cargo cache | **Found, not yet consumed** — no vectors have been pulled into this repo's own `vectors/` directory; doing so would give A6 a correctness oracle before any line of index-pointer code is written |
| A15 (new) | **Upstream tromp/equihash vs. the vendored crate port — verified diff and full commit provenance, not assumed identical.** See the dedicated subsection below (A15 detail) | **Done (verified this session)** — no prior Zebro or Requihash document had checked this; `Zebro/ARCHITECTURE.md`'s "the solver is `tromp::solve_200_9` only" note was accurate about scope but did not note the port's divergence from, or its age relative to, upstream. Zebro documentation updated |
| A16 (new) | **Real memory measurement, executed this session** (`rust/src/bin/req_memcheck.rs`, a counting global allocator wrapping this repo's own `solve_reference`/`solve_arena`): SIZING.md's "naive peak memory" formula underestimates actual measured peak by **20–52×** at (24,5) through (96,5) — e.g. (96,5): formula says 2.00 MB, measured peak is 55.4 MB (reference) / 66.0 MB (arena). Root cause: per-row `Vec<u8>`/`Vec<u32>` heap allocation overhead, which the formula (raw payload bytes only) never modeled. Corroborates BENCHMARK.md's independent time-profiling finding (allocation ~59% of `solve_reference` runtime) via a completely different instrument (memory, not time) | **Done** — `SIZING.md` §2a; this is the one section of that document backed by executed code rather than transcribed formulas. Not yet run past (96,5); (144,5)+ remain unmeasured |

## New direction (this session): quality, concurrency, and sizing over hash-vs-hash

Explicit instruction this session: reduce further focus on cross-hash relative
performance (A4/A4b are considered sufficiently answered on ARM) and instead
prioritize (a) implementation quality, (b) concurrency opportunities, (c)
fitness across the parameter/data-size range SIZING.md just quantified. This
reprioritizes the unblocked-work list below A5:

| Item | What | Depends on |
|---|---|---|
| Q1 | Concurrency audit of `rust/src/solve/parallel.rs` (rayon-gated) — is leaf generation the only parallelized phase, or can the merge/sort rounds be bucket-partitioned across threads too (BENCHMARK.md §6 already names this as unexplored) | Nothing; can start immediately |
| Q2 | Memory-fitness pass using SIZING.md's naive-vs-index-pointer curve: at which (n,k) does this repo's *actual* (not modeled) peak memory cross practical single-machine limits, and does the `arena` backend's real allocation pattern match the modeled "naive" row | Reads SIZING.md; produces measured data to replace or confirm the modeled column |
| Q3 | Implementation-quality review of `rust/src/solve/` and `rust/src/verify/` backend families — dead code, unnecessary clones, error-handling gaps, seam consistency after the A3 `Variant` refactor | Nothing; can start immediately |

These are proposed groupings, not yet executed — the next actionable step in
Group A, pending direction.

**A4/A4b requirement note:** the ARM-only scope is a real limitation, not an
oversight — `blake2b_simd`'s vector paths are x86-only, so the ARM numbers
cannot speak to the AVX2 case. Per your deprioritization this stays an
explicit, undated caveat rather than a task.

**A5 requirements** (unblocking work, not yet started): (a) a memory-capped
solver backend implementing Bernstein truncation over the arena/bucket
solvers already in `rust/src/solve/`; (b) deterministic counters — hashes,
bytes by stride class, peak resident — threaded through `GenProbe` and the
solver backends; (c) the sweep driver (q ∈ memory caps, m ∈ iteration counts)
producing a steepness-vs-q curve per (variant, hash, m). Depends on nothing
in A1–A4b; can start immediately.

### A15 detail: the tromp solver's real provenance, verified via GitHub API commit history

Traced end to end, every date/author/sha pulled directly from the GitHub
commits API (not secondhand) on the three repos involved:
`tromp/equihash`, `zcash/zcash`, `zcash/librustzcash`.

**1. Original integration into zcashd (Oct 2016), by Daira Hopwood.**

| Date (UTC) | Repo | Commit | Author | What |
|---|---|---|---|---|
| 2016-10-20 03:03 | `tromp/equihash` | `690fc5e` | tromp | "tiny speedups" — the exact commit copied, ~3 days after tromp's repo existed |
| 2016-10-20 04:33 | `zcash/zcash` | `ae10ed9c4` | **Daira Hopwood** | "Add Tromp's implementation of Equihash solver (as of tromp/equihash commit 690fc5e...)" — the commit message itself pins the exact upstream sha |
| 2016-10-20 05:03 | `zcash/zcash` | `c7aaab7aa` | **Daira Hopwood** | "Integrate Tromp solver into miner code **and remove its dependency on extra BLAKE2b implementation**" — the very first BLAKE2 swap-out happened 30 minutes after import, same day, same author |
| 2016-10-22 | `zcash/zcash` | `dccc140bf` | Jack Grigg | Comment out tromp's debug print statements (metrics-screen interference) |

**2. What zcashd's copy missed by freezing 3 days into tromp's work — verified
by diffing the pinned sha against tromp's current master (112 commits ahead,
0 behind, as of this check):**

| Date | tromp commit | What it added, that zcashd never got |
|---|---|---|
| 2016-10-27 | `4c463a869`, `d3454d922` | AVX2 support, separate build targets |
| 2016-10-27 | `fc72754de` | 2nd-stage bucketsort → slot linking (algorithmic change) |
| **2016-11-17** | `fec951a2a` | **"add cantor slots enabling 2^10 buckets"** — the Cantor-coding optimization `~/Work/ZK/ZKs/Equihash.md` §2 already credits as tromp's single most consequential contribution, landed 28 days after the code Zcash still ships was frozen |
| 2016-11-17 | `33fed1c9d` | "change equi_miner to 2^10 buckets; obsolete dev_miner" |
| 2017-01-29 → 2017-08-02 | several | Small-`DIGITBITS` and (96,5)-parameter support added |
| 2018-04-16 | `39a91772a` | (192,7) parameter target added |
| 2018-07-10 | `191d3b583` | Command-line BLAKE2b personalization for CPU miners |
| — | — | Two full AVX2 BLAKE2 backends added as new directories: `blake2-asm/` (hand-written asm) and `blake2-avx2/` (`blake2bip.c`, intrinsics) — neither exists in the pinned snapshot |

Net: the code path Zebro depends on today (`equihash` crate → `tromp` module
→ `solve_200_9`) is running solver logic **from before tromp's own
Cantor-coding bucket optimization existed**, not merely "an old version" in
the abstract — a specific, datable, and consequential gap.

**3. Post-import history inside zcashd/librustzcash — three more BLAKE2
swaps, none touching the frozen solver logic itself:**

| Date | Repo | Commit | Author | What |
|---|---|---|---|---|
| 2018-03-02 | `zcash/zcash` | `c938fb1f1` | Daira Hopwood | Large squashed commit (53 files) — general codebase churn, not solver-specific |
| 2020-07-14 | `zcash/zcash` | `2d172e121` | Jack Grigg | Replace libsodium's BLAKE2b with `blake2b_simd` (24 files) — second BLAKE2 swap |
| 2020-10-27 | `zcash/zcash` | `d0a5343da` | Jack Grigg | Lint: include-guard fixes |
| 2021-11-12 | `zcash/zcash` | `e05c1ddf8` | Dimitris Apostolou | Typo fixes |
| 2022-05-26 | `zcash/zcash` | `df08281f2` | Jack Grigg | Migrate BLAKE2b Rust FFI to `cxx` — third BLAKE2 swap |
| 2023-03-08 | `zcash/zcash` | `dd246587a` | Daira-Emma Hopwood | "Fix bit-rotted code in miner tests" |
| 2024-01-04 | `zcash/librustzcash` | `45652a21a` | Jack Grigg | "Import Tromp solver" — re-imports the same frozen zcashd copy into librustzcash, still pinned to the 2016-10-20 snapshot |
| 2024-01-04 | `zcash/librustzcash` | `3aaeb8b71`, `45e7238b8` | Jack Grigg | Convert to compile as plain C; pass `blake2b_simd` bindings as callbacks (fourth BLAKE2 wiring change) |
| 2024-01-11 | `zcash/librustzcash` | `b737d0fe2` | teor | **"Remove unused thread support to enable Windows compilation"** — this is the commit that turned tromp's genuine multi-threading into the single-worker path the crate ships today; done for Windows portability, not performance |
| 2026-05-29 | `zcash/librustzcash` | `e8b9f299d` | Danny Willems | Remove unused variable, most recent touch |

**Reading:** every hand that touched this code after 2016 changed *how it's
wired* (BLAKE2 backend, threading, build system, FFI layer) — four separate
BLAKE2 rewirings and one deliberate threading removal — but nobody resynced
the actual Wagner-search solver logic against tromp's upstream improvements.
`b737d0fe2` names its own reason precisely (Windows build compatibility), so
the single-thread limitation is a known, deliberate, dated trade-off, not an
oversight — but it does mean a faster or GPU-capable dev solver would need to
either restore that removed pthread code or port from tromp's current
upstream state directly, not from what Zebro currently depends on.

## Group B — node track (Zebro)

Full detail and task ordering: `Zebro/ZEBRO.md` §1. Summary for cross-track
visibility only.

| Step | Work | Status |
|---|---|---|
| B1 | Entanglement scorecard: type-taint reachability, dispatch density, constant provenance, co-change coupling | **Not started** — fully designed (`Zebro/ARCHITECTURE.md` §8 point 7), zero tooling written |
| B2 | Identity constants fork (per B1's cut set) + leakage-sweep pin | **Not started** — blocked on B1 |
| B3 | `Solution` parameterization fork (upstream TODO signpost) | **Not started** — independent of B1/B2 |
| B4 | Genesis generator (200,9 today; parameter-general after B3) | **Not started** — independent, could start now at 200,9 |

**B1 requirements** (the concrete next step, unblocking B2): a Rust program,
likely a new `xtask`-style binary or standalone script, that (1) runs
`rustdoc --output-format json` over the pinned `zebra-*` crate tree, (2)
propagates taint from the seed type list (`NetworkUpgrade`, `Network`,
`Sprout*`/`Sapling*`, `FundingStream*`, `Solution`, checkpoint lists,
`Version`) through every signature/trait bound that mentions them, (3) walks
the AST (`syn`) counting `match`/`if let` sites over the seed enums per
module, (4) scans literals against the identity blocklist and a spec-constant
table, (5) mines `git log` for files touched by named protocol-history
commits. Output: a per-crate score feeding the Reuse/Modify/Author decision.
No code exists for any of the five sub-steps as of this writing.

## Group C — bridge

| Step | Work | Status |
|---|---|---|
| C1 | Req mines cross-parameter solution vectors → `zebro-bench` verify curve | **Not started** — depends on SPEC.md's vector format (done) but no miner-to-bench pipeline written |
| C2 | Miner-kernel personalization query (miniZ/gminer/lolMiner community) | **Not started** — no query sent; documented as a to-do in three places (`ARCHITECTURE.md`, `ROADMAP.md`, `ZEBRO.md`), zero outreach performed |
| C3 | D3 selection note draft, synthesizing A4+A5+C1+C2 | **Not started** — blocked on A5 and C1 at minimum |

**C2 requirements** (fully specified, zero execution): identify the
maintainer contact or public issue tracker for miniZ, gminer, and lolMiner;
ask whether custom BLAKE2b personalization strings are already
configuration-exposed or require a per-chain build. This is pure outreach —
no code, no local artifact possible until a response arrives. Long latency;
correctly sequenced to start early and be checked periodically, not blocked
on.

## Cross-track sequencing note

A and B/C are independent by design and can run in parallel. Within A, A5 is
the next unblocked, highest-value step (it's what makes the m-dial and the
Requihash-vs-Equihash steepness claims measurable rather than argued). Within
B, B1 is the sole blocker for B2 and should be prioritized whenever node-track
time is spent.
