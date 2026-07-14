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
| A6 | Compact index-pointer solver backend (unlocks (200,9) composite solves) | **Not started** | Flagged as off the D3 critical path (BENCHMARK.md §8 gap 5) |
| A7 | x86-64/AVX2 leg of A4b | **Deprioritized** | No artifact; dropped per direct instruction. Re-open only if an x86 box becomes available and the AVX2 delta becomes decision-relevant |
| A8 | Solution/memory sizing table, naive vs. index-pointer, Equihash vs. Requihash, k∈{5,7,9} | **Done** | `SIZING.md` |
| A9 | Redirected focus: implementation quality, concurrency, data-sizing fitness (replaces further hash-vs-hash comparison as the primary lab activity) | **In progress** — this document's own next section | Below |

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
