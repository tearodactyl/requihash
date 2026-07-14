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
| A11 (new, corrected twice) | Memory-figure whiplash on Equihash(200,9): **49 MB (original, correct) → wrongly "corrected" to 94 MB on 2026-07-13 (reasoning from the paper's companion notebook, `single_list_ip_mem_estimator`, which does not match the paper's own published Table 3) → restored to 49 MB on 2026-07-14 after reading the actual PDF's Table 3 directly (page 31: 2^28.6 bits ≈ 49 MB, matching Proposition 4's plain O(n·N) bound at constant 1, not Proposition 7's different formula which gives 94 MB)** | **Done, fixed correctly this time** — `SIZING.md` §0a documents the full two-step error trail so it can't recur silently; `~/Work/ZK/ZKs/EquihashSurvey.md` restored to 49 → 223 MB with a citation to the specific table/page. Both formulas (Proposition 4 for Equihash, Proposition 6 for Requihash/Sequihash) now validated against **all seven** of the paper's published Table 3 rows, not one point, before being used to extrapolate the rest of `SIZING.md`'s sweep |
| A12 (new) | Reconciliation gap: tromp's real measured ~144 MB at (200,9) does not match the paper's published 49 MB for the same nominal parameters (~3× gap) | **Partially narrowed (this session)** — `SOLVERS.md` §0.3 found tromp's own README states 144MB directly as his real solver's measured footprint, alongside xenoncat's real 178MB for the same parameters (both real author-stated implementation numbers, not in tension with each other — different bucket-sort/pair-compression choices). What's still **not started**: reconciling either real number against the paper's 49 MB *asymptotic* Proposition 4 bound specifically, which likely requires actually reading/running `equi_miner.c`'s bucket-sizing arithmetic rather than trusting the paper's estimator. See `SIZING.md` §5. The earlier "144 ÷ 9/14 ≈ 224 MB" numeric coincidence (compared against the now-retired 94 MB figure) is retracted along with it — do not re-derive or over-read it |
| A13 (new) | BLAKE2b NEON: the official `BLAKE2/BLAKE2` reference repo ships a maintained `neon/` directory (2018, correctness-patched 2023) — real, usable, aarch64-targeted C code, unlike `blake2b_simd` (Rust crate, x86-only, no build.rs/C-compile step, pure-Rust `#[cfg(target_arch)]` dispatch — see BENCHMARK.md §9 for why this makes "just add NEON like blake3" structurally harder for this specific crate than for blake3's). Real-world caveat found via web search, not yet verified locally: at least one report of BLAKE2b NEON running *slower* than scalar on ARMv8/Cortex-A57 — do not assume NEON is a win here without measuring | **Not started here** — the ZeroPerf plan (`Perf.md` §9.2) is a template this repo could reuse: add a fourth `HashKind`/`LeafHasher` backend, either vendoring `blake2b-neon.c` via FFI or writing a pure-Rust `neon.rs` using `core::arch::aarch64` intrinsics, equivalence-gated exactly like `Blake2bSimd` was, **then actually measured on this machine** before any claim about it helping or hurting |
| A14 (new) | Test-vector source for a ported index-pointer solver (A6): the pinned `equihash` crate (already a Zebro dependency) vendors official Zcash Equihash(96,5)/(200,9) known-answer vectors at `src/test_vectors/valid.rs`/`invalid.rs`, sitting unused in the local Cargo cache | **Done (pulled this session)** — all 46 valid solutions (96,5 / 144,5 / 200,9) and 9 adversarial-invalid vectors extracted from the crate source, `minimal_hex` computed via this repo's own `get_minimal_from_indices` (not reimplemented), round-trip verified (`get_indices_from_minimal` matches the original index lists, 46/46). Written to `vectors/zcash_kat_{96_5,144_5,200_9}.json` and `vectors/zcash_kat_invalid.json`, schema-compatible with SPEC.md §9 plus a `source` field and, on the invalid file, `expect_reject`/`expect_error_kind`. **Marked `keying: "single"`, deliberately** — these are official plain-Equihash KATs (no `i mod k` regularity term), not Requihash vectors; neither this repo's Rust engine nor its C++ header implements `keying=single` yet (SPEC.md §1 confirms it's specified-only), so these vectors are not yet exercised end-to-end by any verifier here. They become the correctness oracle for A6's ported index-pointer solver once single-list keying exists — A6 itself is what's still not started |
| A15 (new) | **Upstream tromp/equihash vs. the vendored crate port — verified diff and full commit provenance, not assumed identical.** Full detail now lives in `SOLVERS.md` (A18) | **Done (verified this session)** — no prior Zebro or Requihash document had checked this; `Zebro/ARCHITECTURE.md`'s "the solver is `tromp::solve_200_9` only" note was accurate about scope but did not note the port's divergence from, or its age relative to, upstream. Zebro documentation updated |
| A17 (new) | Four 2025 papers read directly from PDF (not secondary summaries): full citations, authors, venue status, and cross-paper notes in `PAPERS.md` — includes a second Sequihash-authors paper (2025/2141, Tang/Ding/Sun/Gong) with its own directly-quoted implementation numbers (700 MB / 1.45 GB / 2.5 GB for Equihash(144,5)), distinct from and not yet reconciled against paper 1351's theoretical Table 3 | **Done (reading); reconciliation not started** — `PAPERS.md`, `SIZING.md` §4. Neither this project nor (as far as checked) anyone else has run 2141's own code (`tl2cents/Wagner-Algorithms`) against the Requihash construction |
| A18 (new) | Complete verified commit-level history of `tromp/equihash` — all 143 commits, dated and attributed, plus the full zcashd/librustzcash integration chain and the frozen-snapshot gap — written up in full in `SOLVERS.md` | **Done** | `~/Work/ZK/Requihash/SOLVERS.md`. **Contains one item flagged for the project owner, not resolved by this project**: a GitHub user handle match between this project's own git identity and a 2018 contributor to tromp's repo — see SOLVERS.md §4 |
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
