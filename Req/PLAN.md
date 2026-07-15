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

Purpose, distinct from Groups A-C: build a body of solver implementations
spanning the historical lineage (original authors → tromp's optimized C →
the paper's own Sequihash reference) to gain hands-on expertise with each
design and to measure real hardware parameters (memory, timing) against
implementations this project did not write, as a check on Req's own
numbers. Each item below is scoped for standalone execution by a dedicated
thread with no other context from this document — source paths, exact
incompatibilities found by reading the source, and execution/compatibility
plan are stated in full, not summarized.

| Step | Work | Status |
|---|---|---|
| D1 (RK) | Rust port of Khovratovich's canonical reference (C++) | **Not started** |
| D2 (RT) | Rust port of tromp's C index-pointer algorithm | **Not started** |
| D3 (CS) | Canonical C++ implementation matching the paper's Python Sequihash reference | **Not started** |

### D1 (RK): Rust port of Khovratovich's canonical reference

**Source**: `~/Work/ZK/ZKs/equihash-khovratovich/Source/C++11/{pow.h,pow.cc}`
(117 + 218 lines). CC0-licensed, no attribution required.

**What to do**: Port the `Equihash` class (`InitializeMemory`/
`FillMemory`/`ResolveCollisions`/`FindProof`/`ResolveTree`) into a new,
standalone Rust crate/binary — not inside `rust/src/solve/`, since Req's
own `reference.rs` already independently implements the same naive Wagner
walk; RK's value is a faithful line-for-line port of this specific
historical artifact, not another from-scratch naive solver.

**Parameter range**: matches the original exactly — `n ≤ 32 bytes` (256
bits), any `k` where `Seed`/`LIST_LENGTH` constants hold; no hardcoded
`(n,k)` combinations, since the original is a generic recursive tree-fold
with dynamically-sized `Tuple` vectors (`std::vector<std::vector<Tuple>>`),
not bucket-specialized per-parameter C (that is tromp's design, D2, not
this one). RK's parameter range is *broader* than D2's by construction —
state this explicitly in the port's own docs, since it is easy to assume
the opposite (that the "older" implementation is more limited).

**Execution and binary compatibility**:
- **Vectors, not a live subprocess dependency on the original.** The
  original is CC0 C++11, buildable via its own `Makefile`, so build it
  once, generate a small fixed KAT set (input/nonce/solution triples) at a
  few `(n,k)` points within the original's own recommended families (its
  README: cryptocurrency `(100/110/120,4)`, `(108/114/120/126,5)`;
  client-puzzle `(60/70/80/90,4)`, `(90/96/102,5)`), commit those as
  vectors (same `vectors/*.json` schema this repo already uses for A14),
  and validate the Rust port against the vectors — not a permanent runtime
  dependency on a 2016-era C++11 build.
- **Byte-exact target: the solution (index set), not a wire encoding.**
  The original defines no minimal/compressed encoding at all — it emits
  raw index arrays. Validate on raw index-set equality first; only
  additionally check against this repo's own `get_minimal_from_indices` if
  a wire-format comparison is separately wanted — two separable claims,
  do not conflate them in the port's test suite.

**Exit criteria**: Rust port produces byte-identical index sets to the
vectors generated from the original C++, across every `(n,k)` point
vectored; the port's own README states the parameter range and the
"broader than D2" fact explicitly; no wire-encoding claim made without a
separately-labeled test for it.

### D2 (RT): Rust port of tromp's C index-pointer algorithm

**Source**: the vendored, pinned copy inside the `equihash` crate
(`~/.cargo/registry/.../equihash-0.3.0/tromp/equi_miner.c`) — this exact
snapshot is the one `SOLVERS.md` §0.3/§1-7 and `PLAN.md` A6/A18/A15 already
established as frozen at commit `690fc5eff`, never resynced with upstream;
use this copy, not a fresh clone of `~/Work/ZK/ZKs/equihash-tromp`, so RT's
provenance matches what A6's production backend will eventually be
compared against.

**Parameter range — the specific detail that matters here, verified by
reading the source, not assumed**: tromp's C solver is **not
parametrically general** — it is specialized per `(WN, BUCKBITS,
RESTBITS)` combination via compile-time `#if`/`#elif`/`#error` branches in
`getxhash0`/`getxhash1` and the two bucket-ID computation functions
(`equi_miner.c` lines ~430-455, ~539-551, ~587-654). Concretely, the
vendored `equi_miner.c` only implements: `WN=200` with `RESTBITS ∈
{4,8,9}`, `WN=144` with `RESTBITS=4`, and `WN=96` with `RESTBITS=4` —
anything else fails at **compile time** with `#error not implemented`, not
a runtime error. This is fundamentally different from RK/D1's original,
which is parameter-generic at runtime. A Rust port of RT has two honest
options, and must pick one explicitly rather than blur them:
(a) **mirror the same compile-time specialization** (Rust `const` generics
or a macro expanding the same fixed set of `(n, restbits)` combinations) —
faithful to the original's actual design, equally limited; or
(b) **generalize the bit-extraction logic** to work for arbitrary `(n,
restbits)` — a genuine *improvement* over the original, and must be
labeled as an improvement, not presented as "the same algorithm," if done.

**Execution and binary compatibility**:
- **Running the original source directly is viable and preferred over
  vectors alone**, because the vendored crate is a stable, versioned
  dependency (`equihash-0.3.0`), not a shallow clone with drift risk, and
  this project already has a working precedent for pulling KATs from this
  exact crate (A14). Recommended: build the pinned C via the crate's own
  build script (or a thin CMake wrapper) as a cross-check binary, generate
  vectors from it the same way A14 did, *and* keep the option to invoke it
  directly for a larger differential fuzz pass (many random nonces) beyond
  what a fixed vector set would cover — vectors for CI/regression, live
  execution for one-time deep validation passes.
- **Byte-exact target: the compressed-pair wire encoding, not just the
  index set.** Tromp's triangular-number packing (`x = b(b-1)/2 + s`,
  described in xenoncat's algorithm PDF and mirrored in tromp's
  `tree_from_bid`, `equi_miner.c` lines 94-107) is closer to what this
  repo's own future `solve::pointer` production backend (A6) will need to
  be validated against — RT is the more directly useful of the two ports
  for A6, not just an expertise-building exercise in isolation. State this
  dependency explicitly in RT's own docs so a future A6 pass knows to
  check RT first.

**Exit criteria**: Rust port produces byte-identical solutions to the
pinned C source at every `(WN, RESTBITS)` combination the original
supports; the compile-time-specialization-vs-generalization choice (a) or
(b) is stated explicitly in the port's own docs, not left ambiguous; if the
compressed-pair encoding is ported, it is validated against the original's
own encoding, not merely against the index set.

### D3 (CS): canonical C++ implementation matching the paper's Sequihash reference

**Source**: `~/Work/ZK/ZKs/Generalized-Birthday-Problem/GBP-solver/k_list_algorithm.py`
(273 lines) — the 2025/1351 paper's own runnable k-list Wagner solver.
Genuinely different in convention from Equihash/Requihash, confirmed by
reading the source, not assumed from the paper's prose. `SPEC.md` §10
proposes that the two incompatibilities below (parameter convention,
leaf-string encoding) are not fundamental — CS's own output is the
artifact that would let that proposal be validated empirically once built
(§10.6); this task's scope is unchanged by that proposal, but a future
pass may want to read §10 first for context on *why* the incompatibility
exists.

**Two real, load-bearing incompatibilities to resolve or explicitly
document — found in the source, not the paper's text**:

1. **Parameter convention differs.** The Python reference's `k`
   (`k_list_wagner_algorithm.__init__`, asserting `k` is a power of 2) is
   the paper's own `K` — a *list count* — matching the paper's `(n,
   K=2^k)` table directly, **not** Requihash's `(n,k)` where `k` is
   tree-depth and `2^k` is the *solution size*. `Req/README.md`'s "What
   Requihash changes" section already documents this exact distinction for
   Requihash vs. Equihash's convention; CS needs the equivalent explicit
   note, doubly so since it ports *from* the paper's own convention rather
   than from Equihash's.
2. **Leaf/nonce encoding differs.** The Python reference hashes `nonce +
   f"{i}-{j}".encode()` (`k_list_algorithm.py`'s hash-list generation
   method) — an ASCII decimal string like `b"3-42"` appended to a 16-byte
   nonce — not Requihash's binary `le32(i mod k) || le32(i div k)`. A C++
   port aiming for byte-exact compatibility with this Python reference must
   reproduce the **string formatting** (decimal digits, no leading zeros,
   no fixed width, the literal `-` separator), not translate it into a
   binary encoding — a natural but wrong instinct coming from this repo's
   own C++/Rust convention.

**What "canonical" means here, given the source is explicitly "a basic
implementation" (its own module docstring)**: CS is porting a research
reference, not production code — the Python source mixes `rich`/
`tracemalloc` console-output/profiling scaffolding directly into the
algorithm class (`run_with_memory_trace`, `Console`/`Panel` imports). The
C++ port must separate the algorithm (hash-merge, list generation, solve)
from this presentation/profiling scaffolding, which has no C++ equivalent
worth porting.

**Execution and binary compatibility**:
- **Both vectors and live execution, for different purposes.** Vectors:
  generate a fixed KAT set by running the actual Python reference
  (`k_list_wagner_algorithm.new(n, k, nonce).solve()`) at small `(n,k)`
  points, in a new schema (this is Sequihash's raw `(hash_value,
  index_vector)` output, pre-encoding — not Requihash's wire format, do not
  reuse `vectors/*.json`'s schema unchanged). Live execution: the Python
  reference is confirmed genuinely runnable (`SIZING.md` §0 already
  documents executing it once this project, at a tiny parameter point,
  self-verifying one solution) — a differential harness running the
  Python reference and the C++ port side-by-side on many random nonces is
  feasible and more convincing than a fixed vector set alone at these
  small, fast parameter sizes.
- **Byte-exact target: the index vector only.** There is no minimal/
  compressed wire encoding in the paper's own artifact to match — if CS
  wants a compact encoding downstream, that is a CS-specific design
  decision layered on top, not something to reverse-engineer from the
  Python reference (it defines none).

**Exit criteria**: C++ port produces byte-identical index vectors to the
Python reference (both the vectored KATs and, ideally, a live differential
fuzz pass) at every `(n, K)` point checked; both load-bearing
incompatibilities (parameter convention, leaf-string encoding) are stated
explicitly in the port's own README, not left implicit; the profiling/
presentation-scaffolding separation is done (algorithm code has no
`rich`/console dependency).

## Cross-track sequencing note

A and B/C are independent by design and can run in parallel. Within A, A5 is
the next unblocked, highest-value step (it's what makes the m-dial and the
Requihash-vs-Equihash steepness claims measurable rather than argued). Within
B, B1's core tooling (taint propagation, dispatch density, constant
provenance) is done; B2's cut-set decision can now be made from that data —
B1's remaining step (co-change mining) is a separate, lower-priority tool,
not a blocker.
