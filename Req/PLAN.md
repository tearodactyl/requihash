# PLAN.md — the live work tracker

Authoritative "what's done vs. what's next" for the Requihash program.
Read top to bottom: **§0 orientation → §1 active work (by topic, with
dependencies and order) → §2 done archive → §3 detailed execution
briefs**. Trust this file over any other doc for status.

## 0. Orientation

The program runs as **independent tracks**, coupled only by `SPEC.md` and
the vector-file format (`BENCHMARK.md` §8, "Segregation from node
development"). They can advance in parallel; none gates another:

- **Hash track** — the BLAKE2b/3 primitive (UniBlake). Self-contained.
- **Solver track** — hash/parameter/algorithm research in `Req/rust` and
  the comparative solver corpus (`SOLVER_CORPUS/`). Self-contained.
- **Node track** (`Zebro/`) — identity, genesis, entanglement scorecard.
  Owned by `Zebro/ZEBRO.md` §1; summarized here for cross-track view.
- **Bridge** — items touching two tracks or external parties.

**Status legend:** Done (merged/measured, artifact cited) · Active (work
started, not converged) · Ready (designed, unblocked, zero code) ·
Blocked (waiting on a dependency) · Icebox (deliberately deferred).

**Numbering:** the single live scheme is topic numbers **T1–T7** with
sub-items (`T1.1`…). The old flat per-item scheme (`A1`…`A26`, the
`B`/`C` group letters, and the ad-hoc `Q1–Q3` labels for the quality
briefs) is **retired** — those IDs appear only in §2's *legacy→T
mapping* and the compressed archive, so pre-2026-07-17 citations
elsewhere (e.g. "`PLAN.md` A6") still resolve. The comparative-corpus
port IDs **RZ/RK/RT/CS** are canonical names, not part of the retired
scheme, and stay live (see T5).

---

## 1. Active work (by topic)

### T1 — Hash track: UniBlake (unified C/C++ BLAKE2b/3)

*Design settled (`BLAKE/UniBlake.md`); PoC green on arm64 M4
(`BLAKE/uniblake/`, C99, 6/6 ctests, 0 warnings): U0–U3 + a real NEON
kernel done. (The Rust-side BLAKE2b uniformity decision this extends is
in the §2 archive.)*

| # | Item | Status | Depends on | Notes / artifact |
|---|---|---|---|---|
| T1.1 | U4 — batch/"many" API | Icebox (by design) | T1.2 | The real SIMD win is a batch kernel, not single-message. Materials: `UniBlake.md` §6a |
| T1.2 | U5 — Rust wrapper over UniBlake at Seam A | Ready | U0–U3 (done) | Precondition for ever wiring `Req/rust` to UniBlake, and for the "Seam A flip" the Rust-uniformity decision left open |
| T1.3 | U6 — BLAKE3 adoption | Ready | — | Shape already accommodated, `UniBlake.md` §2a; not built |
| T1.4 | x86 SIMD kernels (SSE4.1/AVX2) | Blocked | real x86 hardware (T4.4) | Needs measurement on real x86 |
| T1.5 | NEON win-list entry | Blocked | weak-ARM measurement (T4.4) | On M4 NEON is 0.55–0.70× (loses); a weak in-order core may win → then promote it in `choose_kernel_from_cpu()` |

**Open question:** should `Req` actually adopt UniBlake? `Req/cpp` is a
direct C→C wire; `Req/rust` needs T1.2 first *and* reverses the recorded
"no Seam A rewiring" decision. **Undecided — owner call.**
(A related idea, "UniTromp" — a UniBlake-analog for the tromp solver —
was raised and dismissed; scope undefined.)

**Order:** T1.2 (Rust wrapper) unblocks the most; then T1.4/T1.5 gate on
T4.4's hardware runs; T1.1/T1.3 are optimization-stage, last.

### T2 — Solver quality, concurrency, and sizing

*The current primary solver-track activity, replacing further
hash-vs-hash comparison (sufficiently answered on ARM). Full standalone
specs for T2.1–T2.3 are in §3.*

| # | Item | Status | Depends on | Notes |
|---|---|---|---|---|
| T2.1 | Concurrency audit of the merge/sort phase (bucket-parallel merge) | Ready | — | `parallel.rs` parallelizes only leaf-gen today; full spec §3 |
| T2.2 | Memory-fitness pass: real vs. modeled peak across the sweep | Ready | reads `SIZING.md` | full spec §3; also finishes the "extend past (96,5)" memory measurement |
| T2.3 | Impl-quality review of `solve/` + `verify/` | Ready | — | dead code, clones, error-handling, seam consistency; full spec §3 |
| T2.4 | Production index-pointer backend | Active (prototype done) | — | `solve/pointer.rs` proven at (48,5)/(72,5); needs counting-sort, KAT-validation, memory-measure to enter `all_solvers()` |

**Order:** T2.1/T2.3 can start immediately and in parallel; T2.2 and T2.4
inform each other (the memory-fitness target is stable once the pointer
backend lands) — do T2.4 then re-run T2.2, or run T2.2 on the current
backends first and re-run after.

### T3 — Security / TMTO steepness (the solver-track headline)

| # | Item | Status | Depends on | Notes |
|---|---|---|---|---|
| T3.1 | Counting harness + memory-capped solver → steepness sweep | Ready | nothing in T1/T2 | **Highest-value solver-track item** — makes the m-dial and the Requihash-vs-Equihash steepness claims *measurable* not argued. Design: `SECURITY_ANALYSIS.md` §8/§8a/§8b |

**Open investigations (deferred, not blocking):** reconcile tromp's real
~144 MB vs. the paper's 49 MB at (200,9) (needs `equi_miner.c` bucket
arithmetic — wait for T2.4); reconcile 2025/2141's numbers vs. 1351
Table 3 (needs running `tl2cents/Wagner-Algorithms`). Both postponed
until this repo's own implementation work is further along.

### T4 — Measurement campaign (profile + benchmark all implementations)

*Filed as task chips 2026-07-17. Every runnable implementation, in
running state, per `BENCH.md` discipline (≥5 reps min/median/MAD,
git+CPU provenance, two-instrument memory, Win/Regression/Noise/New
comparisons, exact params + machine spec). `ub_bench.c` is the in-repo
model for C-native timing.*

| # | Item | Status | Depends on | Notes |
|---|---|---|---|---|
| T4.1 | requihash (`Req/rust`) full run | Ready | — | existing `req_bench`/`req_profile`/`req_memcheck`; gen-vs-merge split, sampling profile, two-instrument memory → `Req/baselines/` |
| T4.2 | `cs-rs` bench | Ready | **add `cs_bench` first** | no bench binary yet (RZ-style gap); add a `reqbench` one mirroring rz/rk, then baseline |
| T4.3 | `rz` + `rk` re-baseline | Ready | **fix `rz_bench` first** | `rz_bench` still single-sample (BENCH.md §2 gap) — bring to reps+provenance+auto-memory, then baseline both |
| T4.4 | uniblake C on x86 + weak ARM | Blocked | hardware / Docker / WSL2 | M4-only numbers are anecdotes (`Platforms.md` §6); x86 exercises the cpuid probe, weak ARM is where NEON might win (feeds T1.4/T1.5) |
| T4.5 | C/C++ references (`Req/cpp/req_bench`, `cs`, `rk/original`, RT) | Ready | — | cross-language C++-vs-Rust comparison + **RT thread-scaling** (`-t 1/2/4/8`) — the comparative-corpus payoff |

**Order:** T4.2 and T4.3 each require a small build step first (add/fix a
bench binary). T4.4 is the one gated on hardware. T4.5 delivers the
corpus's stated purpose (cross-language comparison) and can run now.

### T5 — Comparative solver corpus (ports)

*Full specs/params/validation/kickoff prompts: `SOLVER_CORPUS.md`.
Status verified against `SOLVER_CORPUS/` on disk. The port IDs
RZ/RK/RT/CS are the canonical names (crate dirs + `SOLVER_CORPUS.md`),
not part of the retired A/B/C scheme — they stay.*

| Port | Status | Depends on | Notes |
|---|---|---|---|
| RZ | Done | — | single-core-stripped `equi_miner.c` = Equihash(144,5), byte-identical vs C, on `reqbench`. (200,8/9 out of scope) |
| RK | Done | — | Khovratovich reference, param-generic, 8 KAT vectors byte-exact. (192,7)/(200,9) not attempted ((120,4) alone = 162s/10.5GB) |
| CS | Done (C++ **and** Rust) | — | C++ `cs/` + Rust `cs-rs/` re-port (2026-07-17), both byte-exact vs the 4 Python-reference vectors incl. 2-solution (160,512). C++ is the Rust's differential oracle |
| RT | **Ready — Rust port not started** | — | tromp full multi-core (`pthread_barrier_t`, `-t <nthreads>`). Reference builds natively, trace-identical, thread-invariant at -t 1/2/4. Remaining: cross-check harness, vectors, the port itself (`SOLVER_CORPUS.md` RT section) |

*Discipline note (why RK/RT stalled once): all three were first launched
as parallel agents targeting (200,9) with no build-env check; all killed
mid-run. The settled rule — smallest parameter first, continuous
`STATUS.md` checkpointing, no VM without approval, debug in place — is
now standing (`feedback-agent-dispatch-discipline`). RT's port, when it
starts, follows it.*

### T6 — Node track (Zebro)

*Full detail/order: `Zebro/ZEBRO.md` §1. Cross-track summary only.*

| # | Item | Status | Depends on | Notes |
|---|---|---|---|---|
| T6.1 | Entanglement scorecard | 4/5 done | — | taint/dispatch/constant-provenance done; co-change mining (step 5) needs a GitHub-API tool, lower priority |
| T6.2 | Identity-constants fork | Re-scoped | per-file trigger | premature as a global cut-set; consult the scorecard per-file when a task first needs it (T6.3 is the first trigger) |
| T6.3 | `Solution` parameterization fork | Ready | — | independent; the first real Zebro→Req dependency point |
| T6.4 | Genesis generator | Ready | T6.3 for param-general | could start now at (200,9); design in `Zebro/ARCHITECTURE.md` §8 |

### T7 — Bridge (cross-track / external)

| # | Item | Status | Depends on | Notes |
|---|---|---|---|---|
| T7.1 | Req mines cross-parameter vectors → `zebro-bench` verify curve | Ready | SPEC vector format (done) | no miner-to-bench pipeline yet |
| T7.2 | Miner-kernel personalization query (miniZ/gminer/lolMiner) | Ready (outreach) | — | pure external outreach; long latency — start early, check periodically, don't block on |
| T7.3 | D3 selection-note draft | Blocked | T3.1 + T7.1 | synthesizes the hash-campaign + steepness (T3.1) + vector-curve (T7.1) + kernel-query (T7.2) results |

---

## 2. Done archive (compressed, one line each)

Kept for provenance; the how-we-did-it prose lives in each cited
artifact and in git history. The `A#`/`B#`/`C#` IDs below are the
**retired** old scheme — this archive is where a pre-2026-07-17 citation
of them still resolves.

### 2.1 Legacy ID → topic map (for still-active items)

Old IDs that name work now carried live in §1 map as follows; everything
else on the old list is Done (archived below) or superseded:

| Old ID | Now | Old ID | Now |
|---|---|---|---|
| A5 | T3.1 | B1 | T6.1 |
| A6 | T2.4 | B2 | T6.2 |
| A9 | T2 (topic) | B3 | T6.3 |
| A13 | T1 (NEON, done) / T4.4 (measure) | B4 | T6.4 |
| A16 | T2.2 | C1 | T7.1 |
| A22 | T1 (C/C++ half) | C2 | T7.2 |
| A23 | T1 | C3 | T7.3 |
| A7 | T1.4 + T4.4 | Q1/Q2/Q3 | T2.1/T2.2/T2.3 |

### 2.2 Completed work

**Spec, discipline, infrastructure**
- A1 — byte-exact family spec `PoW(n,k,hash,m,keying,context)` → `SPEC.md`.
- A2 — regression discipline (JSONL, min+median+MAD, decision band) → `rust/src/report.rs`, `baselines/`.
- A3 — BLAKE3 backend + `Iterated` semantics + substitution benches → `rust/src/probe.rs`, `SPEC.md` §5–6.
- A21 — shared `reqbench` measurement crate (stats/provenance/memory), motivated by HECpoc `Bench.md` + ZeroPerf `Perf.md` → `BENCH.md`, `SOLVER_CORPUS/reqbench/`.

**Hash-vs-hash campaigns & naming**
- A4 / A4b — blake2b vs blake3 × m at (96,5)/(144,5)/(200,9); `blake2b_simd` rerun + equivalence gate (ARM only) → `BENCHMARK.md` §9.
- A10 — keep the name "Requihash" (paper's own repo says "Sequihash"), no rename → `SIZING.md` §0.

**Sizing & memory (a settled cluster)**
- A8 — solution/memory sizing table, naive vs index-pointer, k∈{5,7,9} → `SIZING.md`.
- A11 — (200,9) memory-figure correction trail (49→94→49 MB, both formulas validated) → `SIZING.md` §0a.
- A16 — real memory measurement: naive formula underestimates peak 20–52× at (24,5)–(96,5) → `SIZING.md` §2a. (Extension folded into T2.2.)
- A20 — sync `equihash_formulas.py` `SWEEP_POINTS` to `SIZING.md` §2 → verified reproduces every row.

**Provenance, vectors, verification**
- A14 — official Zcash Equihash KAT vectors (`keying:single`) → `vectors/zcash_kat_*.json`.
- A15 — upstream tromp vs vendored crate: verified diff + commit provenance → `SOLVERS.md`.
- A18 — full 143-commit tromp history + zcashd/librustzcash chain → `SOLVERS.md` §4.
- A19 — validate this repo's `expand/compress_array` byte-exact vs the pinned `equihash` crate → `third_party/equihash-0.3.0-vendored/`, `SPEC.md` §4.2/§8.1.
- A25 — **[2026-07-17]** fix Zcash-KAT verification: all 46 KATs now checked vs `equihash::is_valid_solution`; `req_xcheck` routes by keying → lib test + `req_xcheck` PASS.

**Papers**
- A17 — four 2025 papers read from PDF → `PAPERS.md`, `SIZING.md` §4. (Reconciliation deferred → T3 open investigations.)

**BLAKE / UniBlake (this session's big track)**
- A22 — Rust-side BLAKE2b uniformity resolved: `Req/rust`'s 153-line scalar stays incumbent; `blake2ref`/`blake2b_simd` as artifacts → `BLAKE/BLAKE.md` §0. (C/C++ half → T1.)
- A23 (U0–U3 + NEON) — **[2026-07-17]** UniBlake PoC green on M4: persona-carrying reference, CPU probe w/ generation detail, `UB_FORCE_IMPL`, self-test gate (both directions), 3 validation oracle types, versioned ABI-independent snapshot, stress-proven NEON kernel (measured slower on M4 → registered-not-defaulted). Cross-platform build paths documented (`uniblake/DEPLOY.md`), unrun. Companion refs: `Platforms.md`, `kernel-neon-refs/`. → `BLAKE/UniBlake.md`, `BLAKE/uniblake/`.
- A24 — **[2026-07-17]** persona stem `"ReqhashPoW"`(10) → `"ReqPoW"`(6)+4 reserved; full construction migration + vector regen; Rust 19/19, uniblake 6/6 → `SPEC.md` §3.
- A26 — **[2026-07-17]** Rust warning cleanup: 4 feature-gated false positives fixed with precise `#[cfg]` gating; our code 0 warnings all configs (only the pinned vendored-crate C warning remains).

**Deprioritized / icebox (settled non-work)**
- A7 — x86-64/AVX2 leg: dropped unless real x86 + a decision-relevant AVX2 question both appear. (Now tracked live as T1.4/T4.4.)
- A13 — BLAKE2b NEON backend: iceboxed as optimization-stage. (Superseded — a NEON kernel now exists under A23/T1; the *measurement* is T4.4.)
- *(Items still in flight — A5, A6, A9, A12, B1–B4, C1–C3 — are carried live in §1; see the §2.1 map.)*

---

## 3. Detailed execution briefs

Standalone specs for T2.1–T2.3, written so a dedicated thread can
execute one with no other context from this document. The T2 table in
§1 is the index into these.

### T2.1 — Concurrency audit of the merge/sort phase

**Scope**: Determine whether `rust/src/solve/parallel.rs`'s rayon
parallelism can extend from leaf-generation-only to the merge/sort
rounds, and if so, implement it.

**Current state** (verified): `parallel.rs` (32 lines) parallelizes only
`solve_arena_with_leaves`'s leaf-fill closure via
`hashes.par_chunks_mut(full).enumerate().for_each(...)` — the merge runs
through `solve_arena`'s sequential path. `bucket.rs`'s counting-sort
merge is single-threaded end to end. `BENCHMARK.md` §6 already names
bucket-parallel merge as "the natural parallel decomposition rayon's
generation-only solver could not reach."

**What to do**: (1) read `bucket.rs` in full, identify the parallelizable
`while b < nbuckets` bucket-scan loop (buckets are independent post
counting-sort). (2) Implement a `solve::bucket_parallel` backend that
rayon-parallelizes per-bucket pairing, keeping the counting-sort
partition sequential. (3) Add to `all_solvers()`, confirm
`all_solvers_agree`. (4) Benchmark vs `bucket.rs`'s single-threaded
80.0ms at (96,5) via `req_bench`; report speedup at the machine's thread
count.

**Guidelines**: don't mutate `bucket.rs`'s existing single-threaded
solver (add a new backend); follow its counting-sort exactly; match
`parallel.rs`'s doc-comment style.

**Exit**: registered in `all_solvers()`, passes `all_solvers_agree` at
(48,5)/(72,5)/30 nonces, measured speedup in `BENCHMARK.md` with method +
thread count, `ARCHITECTURE.md` §7 technique table updated.

### T2.2 — Memory-fitness: real vs. modeled peak memory

**Scope**: extend `rust/src/bin/req_memcheck.rs`'s real-allocator
measurement past its (96,5) ceiling; find where actual peak crosses
practical single-machine limits.

**Current state** (verified): `req_memcheck.rs` (184 lines) is a
counting-global-allocator harness measuring
`solve_reference`/`solve_arena` real peak vs `SIZING.md`'s naive formula,
exercised only at (24,5)–(96,5) (`SIZING.md` §2a). Larger rows are
arithmetic, unexercised.

**What to do**: (1) read `req_memcheck.rs` + `SIZING.md` §2a (don't
redesign the method — extend the range). (2) Run at each successive sweep
point (k=5: 120…216; k=7: 128…232; k=9: 160…240) until the machine can't
without swapping — stop and report that as a finding. (3) Record measured
peak (reference + arena), the naive prediction, and the ratio (does the
20–52× hold/grow/shrink?). (4) Update `SIZING.md` §2a marked "Measured".

**Guidelines**: measurement not modeling — don't extrapolate; an OOM/
too-slow point is itself a data point.

**Exit**: `SIZING.md` §2a extended with measured rows up to the largest
clean (n,k); one sentence stating the practical ceiling; the 20–52×
pattern confirmed or corrected. (Sequenced after T2.4/T2.1–T2.3 so the
target is stable — see T2 order note.)

### T2.3 — Impl-quality review of `solve/` and `verify/`

**Scope**: audit `rust/src/solve/` and `rust/src/verify/` for dead code,
unnecessary clones, error-handling gaps, seam consistency after the A3
`Variant` refactor.

**What to do**: (1) read every file in both dirs in full
(`solve/{reference,arena,bucket,parallel,pointer}.rs`,
`verify/{reference,arena,early}.rs`, both `mod.rs`). (2) Check for: (a)
`.clone()` where a borrow/move would do (`bucket.rs`'s `idxs[ra].clone()`
is a candidate); (b) dead code unreachable from `all_solvers()`/
`all_verifiers()`/a test; (c) error-handling consistency across
`reference`/`arena`/`early` verifiers; (d) whether the `Solver`/`Verifier`
trait shape is still right after `pointer.rs` (which doesn't implement the
trait). (3) Produce a written findings list (file:line, issue, fix)
FIRST. (4) Apply only fixes that can't change solver/verifier behavior;
anything touching a benchmarked hot path needs a re-bench, not just a
re-test.

**Guidelines**: quality/hygiene, not perf — don't chase micro-opts;
preserve every doc comment's technical content (load-bearing history).

**Exit**: written findings list delivered first; applied fixes pass the
full suite; no fix changes measured output (re-run `all_solvers_agree`/
`all_verifiers_agree` after any change).

---

## 4. Requirements notes (carried from prior versions)

**A5 / T3.1 requirements** (unblocking, unstarted): (a) a memory-capped
solver backend implementing Bernstein truncation over the arena/bucket
solvers; (b) deterministic counters (hashes, bytes by stride class, peak
resident) threaded through `GenProbe` and the backends; (c) the sweep
driver (q ∈ memory caps, m ∈ iteration counts) producing a
steepness-vs-q curve per (variant, hash, m). Depends on nothing in
T1/T2.

**B1 / T6.1 requirements** (next concrete step): a Rust program (xtask or
standalone) that (1) runs `rustdoc --output-format json` over the pinned
`zebra-*` tree, (2) propagates taint from the seed type list, (3) walks
the `syn` AST counting `match`/`if let` over the seed enums, (4) scans
literals against the identity blocklist + spec-constant table, (5) mines
`git log` for protocol-history commits. Co-change mining (step 5) is the
one unbuilt piece.

**C2 / T7.2 requirements** (fully specified, zero execution): identify
the maintainer contact / issue tracker for miniZ, gminer, lolMiner; ask
whether custom BLAKE2b personalization is config-exposed or needs a
per-chain build. Pure outreach — no local artifact until a response
arrives.

**A4/A4b caveat**: the ARM-only scope is real, not an oversight —
`blake2b_simd`'s vector paths are x86-only, so ARM numbers can't speak to
AVX2. Per the deprioritization this stays an explicit, undated caveat,
now tracked live as T1.4/T4.4.
