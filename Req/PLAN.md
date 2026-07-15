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
| A5 | Counting harness + memory-capped solver → TMTO steepness sweep | **Not started** | Design only: `SECURITY_ANALYSIS.md` §8 (items 2-3, 8-9), §8a. Four implementation/harness-quality assumptions the result depends on: §8b |
| A6 | Compact index-pointer solver backend (unlocks (200,9) composite solves) | **Design + prototype done; production backend not started** | `rust/src/solve/pointer.rs` — proves the cross-round pointer-tree representation (8-byte `Ptr` per row vs. a growing `Vec<EhIndex>`) reconstructs correctly against `solve_reference` and the production verifier at (48,5)/(72,5), 30 nonces each, own test passing. Deliberately not in `all_solvers()`: uses a plain sort instead of `bucket.rs`'s counting sort, not KAT-validated (A14), not memory-measured. Off the D3 critical path (`BENCHMARK.md` §8 gap 5) |
| A7 | x86-64/AVX2 leg of A4b | **Deprioritized** | Dropped per direct instruction; re-open only if an x86 box and a decision-relevant AVX2 question both appear |
| A8 | Solution/memory sizing table, naive vs. index-pointer, Equihash vs. Requihash, k∈{5,7,9} | **Done** | `SIZING.md` |
| A9 | Redirected focus: implementation quality, concurrency, data-sizing fitness (replaces further hash-vs-hash comparison as the primary lab activity) | **In progress** | Below (§"Current direction") |
| A10 | Naming: source paper's own artifact repo calls the construction "Sequihash"; project keeps "Requihash" by explicit decision | **Done, no rename** | `SIZING.md` §0 |
| A11 | Equihash(200,9) memory-figure correction trail: 49 MB → wrongly 94 MB → restored to 49 MB, both formulas now validated against all seven of the paper's Table 3 rows | **Done** | `SIZING.md` §0a |
| A12 | Reconciliation gap: tromp's real ~144 MB vs. the paper's asymptotic 49 MB at (200,9) | **Partially narrowed; further reconciliation explicitly postponed** — the 144MB/178MB pair (tromp vs. xenoncat) are both real, non-conflicting implementation figures. Closing the remaining gap needs reading/running `equi_miner.c`'s bucket-sizing arithmetic — deferred until A6's production backend exists, so the comparison target is stable rather than a moving prototype | `SOLVERS.md` §0.3, `SIZING.md` §5 |
| A13 | BLAKE2b NEON backend (aarch64) — official `BLAKE2/BLAKE2` repo ships a maintained `neon/` dir; unverified report of it running slower than scalar on some hardware | **Repo cloned and verified (`~/Work/ZK/ZKs/blake2-reference`, `neon/blake2b-neon.c` + headers, 908 lines, CC0); backend not started** | Template: ZeroPerf `Perf.md` §9.2; caveat detail: `BENCHMARK.md` §9. Two separate gaps, of different sizes: the automaticity mechanism (a `#[cfg(target_arch = "aarch64")]` gate, trivial once NEON code exists in Rust) vs. the NEON code itself, which needs a genuine translation from C `arm_neon.h` intrinsics to Rust `core::arch::aarch64` intrinsics — `blake2b_simd` has no `build.rs`/C-compilation hook to bolt an FFI wrapper onto, unlike `blake3`'s build-script-driven dispatch, so vendoring the C file directly is not a shortcut here; someone has to write the Rust translation |
| A14 | Official Zcash Equihash KAT vectors pulled from the pinned `equihash` crate, `keying: "single"` (plain Equihash, not Requihash) | **Done** | `vectors/zcash_kat_{96_5,144_5,200_9,invalid}.json`; correctness oracle for A6 once single-list keying exists (`SPEC.md` §1) |
| A15 | Upstream tromp/equihash vs. the vendored crate port — verified diff and commit provenance, not assumed identical | **Done** | `SOLVERS.md` (§A18 below); Zebro's `ARCHITECTURE.md` updated to cite it |
| A16 | Real memory measurement (`rust/src/bin/req_memcheck.rs`): naive peak-memory formula underestimates actual peak by 20–52× at (24,5)-(96,5) | **Done; extension past (96,5) postponed** — folded into Q2 below, sequenced after A6/Q1-Q3 land so the measurement target is stable | `SIZING.md` §2a |
| A17 | Four 2025 papers read directly from PDF; includes 2025/2141's own implementation numbers, not yet reconciled against 1351's Table 3 | **Done (reading); reconciliation explicitly postponed** — needs running `tl2cents/Wagner-Algorithms`' own code, deferred until this repo's own implementation work (A6, Q1-Q3) is further along | `PAPERS.md`, `SIZING.md` §4 |
| A18 | Complete verified commit-level history of `tromp/equihash` (143 commits) plus zcashd/librustzcash integration chain | **Done** | `~/Work/ZK/Requihash/SOLVERS.md`. One item flagged for the project owner (a GitHub identity match), unresolved — `SOLVERS.md` §4 |
| A19 | Close the validation gap between real zcashd byte output and this repo's own `expand_array`/`compress_array` reimplementations — `SPEC.md` §4.2/§8.1 currently state "written to be interoperable, byte-accurate," not "verified," because the only real Zcash KATs pulled so far (A14) are `keying=single`, which this engine doesn't implement | **Not started** | Needs either (a) implementing `keying=single` first so A14's existing vectors become usable end-to-end, or (b) a smaller standalone check: run zcashd's actual `ExpandArray`/`CompressArray` (or the pinned `equihash` crate's `expand_array`/`compress_array` in `minimal.rs`, same byte layout) against this repo's `rust/src/lib.rs::expand_array`/`get_minimal_from_indices` on the same raw bytes, independent of full solve/verify. (b) is the faster path to closing this specific gap without waiting on `keying=single` |
| A20 | `scripts/equihash_formulas.py`'s `SWEEP_POINTS` table is out of sync with `SIZING.md`'s actual current sweep — the script still has the pre-expansion point set (missing 96/120/144 for k=5, 128/192 for k=7, 160/200 for k=9; still includes the removed k=9 n=360 point) | **Not started** | `scripts/equihash_formulas.py` `SWEEP_POINTS` dict needs updating to match `SIZING.md` §2's table exactly, or the script's own output should be treated as the source of truth and `SIZING.md` regenerated from it — pick one direction, don't let the two drift independently again |
| A21 | Shared measurement discipline (`reqbench` crate) for `SOLVER_CORPUS` ports — motivated by HECpoc's `Bench.md` (provenance/canonical-evidence discipline) and ZeroPerf's `Perf.md` (cross-checking every number against a second instrument, exact window/parameter reporting) reviewed and applied here; RZ's first bench pass had none of this (single-sample timing, no git provenance, one manual memory cross-check) | **Done** | `BENCH.md` (the spec), `SOLVER_CORPUS/reqbench/` (the crate — statistics, provenance stamping, memory cross-check), `SOLVER_CORPUS/rz/src/bin/rz_bench.rs` (migrated, now 7-rep min/median/MAD + provenance + automated RSS cross-check), `SOLVER_CORPUS/_template/` (skeleton for RK/RT/CS). `Req/rust`'s own `report.rs` stays a separate, intentionally-uncoupled sibling implementation — see `ARCHITECTURE.md` §8 for why |

## Current direction: quality, concurrency, and sizing over hash-vs-hash

A4/A4b are considered sufficiently answered on ARM, so further cross-hash
relative-performance comparison is deprioritized in favor of (a)
implementation quality, (b) concurrency opportunities, (c) fitness across the
parameter/data-size range SIZING.md quantified. This reprioritizes the
unblocked-work list below A5:

| Item | What | Depends on |
|---|---|---|
| Q1 | Concurrency audit of `rust/src/solve/parallel.rs` (rayon-gated) — is leaf generation the only parallelized phase, or can the merge/sort rounds be bucket-partitioned across threads too (BENCHMARK.md §6 already names this as unexplored) | Nothing; can start immediately |
| Q2 | Memory-fitness pass using SIZING.md's naive-vs-index-pointer curve: at which (n,k) does this repo's *actual* (not modeled) peak memory cross practical single-machine limits, and does the `arena` backend's real allocation pattern match the modeled "naive" row | Reads SIZING.md; produces measured data to replace or confirm the modeled column |
| Q3 | Implementation-quality review of `rust/src/solve/` and `rust/src/verify/` backend families — dead code, unnecessary clones, error-handling gaps, seam consistency after the A3 `Variant` refactor | Nothing; can start immediately |

These are proposed groupings — the full spec for each (below) is written for
standalone execution by a dedicated thread with no other context from this
document.

### Q1: Concurrency audit of the merge/sort phase

**Scope**: Determine whether `rust/src/solve/parallel.rs`'s rayon parallelism
can be extended from leaf-generation-only to the merge/sort rounds, and if
so, implement it.

**Current state** (verified): `parallel.rs` (32 lines) parallelizes only
`solve_arena_with_leaves`'s leaf-fill closure via
`hashes.par_chunks_mut(full).enumerate().for_each(...)` — the merge itself
runs through `solve_arena`'s existing sequential path. `bucket.rs`'s
counting-sort merge (used by `BucketSolver`, not `ParallelSolver`) is
single-threaded end to end. `BENCHMARK.md` §6 already names bucket-parallel
merge as "the natural parallel decomposition rayon's generation-only solver
could not reach."

**What to do**:
1. Read `rust/src/solve/bucket.rs` in full; identify the exact loop that's
   parallelizable (the `while b < nbuckets` bucket-scan loop) — buckets are
   independent once counting-sort has partitioned rows into them, so each
   bucket's pairing work has no cross-bucket data dependency.
2. Implement a `solve::bucket_parallel` (or extend `parallel.rs`) backend
   that rayon-parallelizes the per-bucket pairing step, keeping the
   counting-sort partition step itself sequential (it's a single pass,
   likely not worth parallelizing at these parameter sizes).
3. Add it to `all_solvers()` and confirm it passes `all_solvers_agree`.
4. Benchmark against `bucket.rs`'s existing single-threaded numbers
   (BENCHMARK.md §6's 80.0ms baseline at (96,5)) using the existing
   `req_bench` harness — report speedup at whatever thread count the test
   machine has.

**Guidelines**: Do not change `bucket.rs`'s existing single-threaded solver
— add a new backend, don't mutate the baseline others compare against.
Follow `bucket.rs`'s exact counting-sort logic for the partition step
(don't redesign it). Match `parallel.rs`'s existing doc-comment style
(explains what's parallel, what isn't, and why).

**Exit criteria**: New backend registered in `all_solvers()`, passes
`all_solvers_agree` at (48,5)/(72,5)/30 nonces (same gate as every other
backend), measured speedup reported in `BENCHMARK.md` with method and
thread count stated, `ARCHITECTURE.md` §7's technique table updated if
this is deemed a meaningful step toward "in-place merge" or treated as a
new row.

### Q2: Memory-fitness pass — real vs. modeled peak memory

**Scope**: Extend `rust/src/bin/req_memcheck.rs`'s real-allocator
measurement past its current (96,5) ceiling, and determine at which (n,k)
actual peak memory crosses practical single-machine limits.

**Current state** (verified): `req_memcheck.rs` (184 lines) is a
counting-global-allocator harness already measuring
`solve_reference`/`solve_arena` real peak vs. `SIZING.md`'s naive formula,
currently only exercised at (24,5) through (96,5) (`SIZING.md` §2a,
`PLAN.md` A16). Every larger (n,k) row in `SIZING.md`'s sweep table is
"pure arithmetic, unexercised."

**What to do**:
1. Read `req_memcheck.rs` in full and `SIZING.md` §2a for the exact
   methodology already established (don't redesign the measurement
   approach — extend its parameter range).
2. Run it at each successive (n,k) point already in `SIZING.md`'s sweep
   table (k=5: 120, 144, 168, 192, 216; k=7: 128, 168, 192, 232; k=9: 160,
   200, 240) up to whatever the test machine's memory allows without
   swapping — stop and report the point where it becomes impractical,
   don't force it.
3. At each point, record: measured peak (reference and arena backends),
   the SIZING.md "naive formula" prediction for the same point, and the
   ratio (continuing the 20-52× pattern already found at smaller params —
   does the ratio hold, grow, or shrink as n/k increase?).
4. Update `SIZING.md` §2a's table with the new rows, clearly marked
   "Measured" per its own evidence-grading convention (§1) — do not touch
   the still-unmeasured larger rows' "Formula only" status if they remain
   out of reach.

**Guidelines**: This is a measurement task, not a modeling task — don't
extrapolate or curve-fit; report only what was actually run. If a
parameter point OOMs or takes impractically long, stop and record that as
a finding (a real "practical limit crossed" data point), not a failure to
work around.

**Exit criteria**: `SIZING.md` §2a's table extended with real measured
rows up to the largest (n,k) that ran cleanly on the test machine; one
sentence stating the practical ceiling found (e.g., "OOM/impractical past
(n,k) = X on this machine, Y GB RAM"); the 20-52× ratio pattern either
confirmed to continue, or a corrected finding if it doesn't. (Postponed
per direct instruction until A6/Q1-Q3 are further along, so the
measurement target is stable — see A16's status row.)

### Q3: Implementation-quality review of solve/ and verify/

**Scope**: Audit `rust/src/solve/` (6 files once Q1 lands) and
`rust/src/verify/` (4 files, 215 lines) for dead code, unnecessary clones,
error-handling gaps, and seam consistency after the A3 `Variant` refactor.

**What to do**:
1. Read every file in both directories in full (they're small; this is
   tractable in one pass):
   `solve/{reference,arena,bucket,parallel,pointer}.rs`,
   `verify/{reference,arena,early}.rs`, plus both `mod.rs` files.
2. Check specifically for: (a) `.clone()` calls where a reference or move
   would do — `bucket.rs`'s `idxs[ra].clone()` pattern in the merge step is
   a specific candidate worth checking against arena's approach; (b) dead
   code — anything not reachable from `all_solvers()`/`all_verifiers()` or
   a test; (c) error handling — do the `Verifier::verify` implementations
   across `reference`/`arena`/`early` return consistent `Error` variants
   for the same failure class, or did `early`'s fused-pass design (per
   `verify/mod.rs`'s doc comment) drift from the other two's error
   granularity; (d) whether the `Solver`/`Verifier` trait boundary itself
   is still the right shape after `pointer.rs`'s addition — does
   `pointer.rs`'s prototype-only status (not implementing the `Solver`
   trait at all) suggest the trait needs a variant, or is staying outside
   it correct until production-hardened.
3. Produce a written findings list (file:line, issue, suggested fix), not
   a silent series of edits — this is a review deliverable first, edits
   second, so the fixes can be checked against the existing correctness
   gates before landing.
4. Apply only the fixes that don't risk changing solver/verifier behavior
   (e.g., removing an actually-dead function, replacing a clone with a
   borrow where lifetimes allow) — anything that touches the hot path of a
   solver already measured in `BENCHMARK.md` needs a re-benchmark before
   landing, not just a re-test.

**Guidelines**: This is quality/hygiene work, not a performance task —
don't chase micro-optimizations that would require re-running the whole
benchmark suite for a marginal win. Preserve every existing doc comment's
technical content; only touch code, not the module-level explanations
(they're load-bearing project history in several places, e.g. `bucket.rs`'s
header).

**Exit criteria**: A written findings list delivered first (even if some
findings are "no action, flagged for awareness"); applied fixes pass the
full existing test suite (`cargo test --lib`, all tests as of this
session); no fix changes a solver's or verifier's measured output (re-run
`all_solvers_agree`/`all_verifiers_agree` after any change touching those
modules).

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
| B1 | Entanglement scorecard: type-taint reachability, dispatch density, constant provenance, co-change coupling | **4/5 done** — `Zebro/zebro-entangle` implements taint propagation, a dispatch-density proxy, and a constant-provenance collision check (split into specific-vs-degenerate to avoid false alarms on generic 0/all-ones matches). Run against pinned `zebra-chain` 10.1.0: 98/3278 items (3.0%) transitively tainted; 3 specific constant collisions found, all expected (Zcash's own version-group IDs). Co-change mining (step 5) not built — needs a separate GitHub-API tool, not local git history — `Zebro/ARCHITECTURE.md` §8 point 7 |
| B2 | Identity constants fork (per B1's cut set) + leakage-sweep pin | **Re-examined; premature as a standalone item** — checked directly: Zebro's own authored code doesn't `use zebra_chain::*` anywhere yet (Phase 0, `zebrad` reused wholesale). B1's scorecard measures potential taint in `zebra-chain`'s API, not current exposure in authored code — forking now would be ahead of any real reuse pressure. Corrected process: consult the scorecard per-file when a concrete task first needs that file (B3 is the first such trigger, for `equihash.rs`'s `Solution`), not as a one-time global cut-set. `Zebro/ARCHITECTURE.md` §8 |
| B3 | `Solution` parameterization fork (upstream TODO signpost) | **Not started** — independent of B1/B2 |
| B4 | Genesis generator (200,9 today; parameter-general after B3) | **Not started** — independent, could start now at 200,9. Detailed design (language, structure, the 200,9-vs-Req-solver dispatch fork, code reuse, why no feature-flag matrix is needed) in `Zebro/ARCHITECTURE.md` §8 — this is the first real Zebro-to-Req dependency, worth noting explicitly |

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

## Group D — comparative solver corpus

Full task specs, parameter ranges, validation plans, and kickoff prompts:
`SOLVER_CORPUS.md`. Status below, verified directly against
`SOLVER_CORPUS/` on disk, not assumed from prior notes.

| Port | Status | Detail |
|---|---|---|
| RZ | **Done** — the only one of four actually finished | Native Rust port of the vendored, single-core-stripped `equi_miner.c` at `(WN=144, WK=5, RESTBITS=4)` = Equihash(144,5). Cross-checks byte-identical against the C original across 3 nonces (`tests/cross_check.rs`, both debug and release). Now on the shared `reqbench` measurement harness (A21): 7-rep min/median/MAD timing, git-provenance-stamped, peak memory (6.27 GB) cross-checked against OS RSS and agreeing. `(200,8)`/`(200,9)` explicitly out of scope for this pass — a deliberate, stated deferral, not a gap |
| RK | **Not started** — zero code on disk | Khovratovich's original C++ reference solver, parameter-generic (broader range than RT by construction — no compile-time `(n,k)` restriction). An earlier attempt to build this in parallel with RZ/RT was aborted (see "History" below) with nothing committed; needs a fresh start, smallest-parameter-first |
| RT | **Not started** — zero code on disk; genuinely blocked, not just unattempted | Tromp's full multi-core `equi_miner.h`/`equi_miner.cpp` (real `pthread_barrier_t` round sync, `-t <nthreads>`). Blocked on `blake2-avx2/blake2bip.h` — real x86 AVX2 intrinsics with no ARM equivalent, so it will not build natively on this Apple Silicon machine. An in-place fix (e.g. RZ's own BLAKE2b-glue substitution trick) has not been tried yet; a Linux/x86_64 VM was proposed once and explicitly not approved (`feedback-agent-dispatch-discipline` memory: no VM without a clear stated reason and explicit go-ahead) |
| CS | **Not started** — zero code on disk | C++ port of the paper's own Python "Sequihash" k-list reference (`k_list_algorithm.py`). Two load-bearing convention differences from this project's own code (list-count `k` not tree-depth; ASCII `f"{i}-{j}"` leaf encoding not binary `le32`) must be preserved exactly, not silently translated — `SOLVER_CORPUS.md`'s own CS section states both explicitly |

**History, for context on why RK/RT stayed at zero for two sessions
running:** RK, RZ, and RT were originally launched as three parallel
background agents, each targeting a hard parameter tier including
`(200,9)`, with no prior confirmation the build environment could even
compile the C/C++ references on this machine. All three had to be killed
mid-run — RT hit the AVX2 blocker above with no fallback tried; RK hung
debugging a `(200,9)` vector-generation subprocess with nothing
checkpointed; RZ alone had partial, real progress (a working C
cross-check harness) but no Rust code yet. The corrected approach —
smallest parameter first, continuous `STATUS.md` checkpointing, no VM
without explicit approval, debug in place rather than reaching for new
environments — is what RZ's second, successful pass followed, and is now
the standing discipline (`feedback-agent-dispatch-discipline` memory,
`SOLVER_CORPUS.md`'s cross-cutting requirements). RK and RT should restart
under that same discipline, not resume from where the aborted attempt left
off.

## Cross-track sequencing note

A and B/C are independent by design and can run in parallel. Within A, A5 is
the next unblocked, highest-value step (it's what makes the m-dial and the
Requihash-vs-Equihash steepness claims measurable rather than argued). Within
B, B1's core tooling (taint propagation, dispatch density, constant
provenance) is done; B2's cut-set decision can now be made from that data —
B1's remaining step (co-change mining) is a separate, lower-priority tool,
not a blocker.
