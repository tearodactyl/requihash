# BENCH.md — measurement discipline for `Req/` and `Req/SOLVER_CORPUS/`

What every benchmark/memory/profiling result in this tree should carry, and
why — a shared discipline applied to `Req/rust`'s own harness
(`req_bench.rs`/`req_memcheck.rs`/`req_profile.rs`, `baselines/*.jsonl`,
`report.rs`) and to every port under `Req/SOLVER_CORPUS/` (RZ today; RK/RT/CS
as they're built). This document does not replace `BENCHMARK.md` (Req's own
measured results) or any port's own `STATUS.md`/`README.md` — it states the
*method* those documents' numbers should already be following, and gives new
ports a template instead of each improvising one (RZ's own `rz_bench.rs` was
built ad hoc in an earlier session; this document is what a second pass at
it should have started from).

**Two source methodologies this borrows from directly, not reinvented:**
`~/Work/Spank/HECpoc/Bench.md` (a file-based assessment-bundle discipline
for a Rust HEC receiver — canonical vs. derived evidence, provenance
stamping, schema-controlled tables, explicit validation as its own step) and
`~/Work/ZK/ZeroPerf/Perf.md` (real C++/Rust profiling of `zerod`'s sync
path — cross-checking every number against a second source, reporting the
exact window/parameters a result covers, an explicit A/B noise floor and
decision rule, and §5 specifically: Equihash verification cost on this same
Apple Silicon hardware family, root-caused to a missing NEON blake2b
backend — directly relevant precedent for anything blake2b-shaped this
project measures).

## 1. What a result must carry (provenance)

Every number reported anywhere — in a `STATUS.md`, a `baselines/*.jsonl`
line, a chat response — should be traceable back to exactly what produced
it. Minimum fields, borrowed from HECpoc's `run.toml` pattern:

- **Git identity**: commit hash + dirty-tree flag, for the repo the binary
  was built from.
- **Build identity**: release vs. debug, and for anything performance-
  sensitive, confirmation of `--release`/`-O`-equivalent flags — a debug-
  build number silently reported as if it were a release number is exactly
  the kind of confusion this section exists to prevent (RZ's own STATUS.md
  already had to caveat this once: "expected at debug opt level").
- **Parameter identity**: the exact `(WN,WK,RESTBITS)` or `(n,k)` (never the
  ambiguous bare-pair shorthand — see `SOLVER_CORPUS/rz/README.md`'s own
  naming note) plus input/nonce identity (a hash or the literal bytes, not
  "the usual test input").
- **Machine identity**: architecture (`std::env::consts::ARCH` is already
  captured by `Req/rust`'s `report.rs`), and for anything comparing across
  runs, enough context to know if it's the same physical machine.

A result missing any of these is provisional, not a baseline — usable to
report "this looks about right" but not to detect a future regression
against, because there's nothing to re-derive it from later.

## 2. Repeated trials, not single samples

**Single-sample timing numbers are not measurements, they're anecdotes.**
`Req/rust`'s own `report.rs` already gets this right: min + median + MAD
over multiple warm reps, because for a deterministic CPU-bound kernel,
timing noise is strictly additive, so the minimum estimates true cost while
the median guards against bimodality and the MAD gives the within-run noise
floor (`report.rs`'s own doc comment states this precisely — read it before
reimplementing).

**RZ's own `rz_bench.rs` (built last session) does not yet do this** — it
runs each nonce exactly once. That's the concrete gap this document and the
`reqbench` crate (§4) close. Any new port's bench binary should default to
at least 5 reps per case, following `req_bench.rs`'s own warm-up-by-time-
budget pattern (busy-loop for ~50ms before timing starts, so a microbench
doesn't finish entirely on an efficiency core before the scheduler migrates
it — a real, previously-hit gotcha, not a hypothetical one).

## 3. The regression decision rule

Borrowed directly from `report.rs` and independently re-derived by
`BENCHMARK.md`'s own regression workflow and Perf.md §3's fd-cache A/B: a
delta only counts as a real win/regression if it exceeds
`max(this_run_MAD, baseline_MAD, band_pct% of baseline_min)`. The relative
floor exists because same-code cross-process spread on an unpinned machine
routinely reaches 3-7% (`BENCHMARK.md`'s own measured figure) — far above
within-run MAD — so a naive "any decrease is a win" comparison produces
constant false positives. Perf.md's fd-cache A/B (§3) is a second,
independent confirmation of the same lesson from a completely different
codebase: a measured "+1.09%" difference was correctly reported as "not
distinguishable from noise" because it didn't clear a stated `t ≈ 2.5-2.6`
significance bar, not because it looked unimpressive.

**Standing rule: report every comparison as Win/Regression/Noise/New per
this rule, never as a bare percentage.** A percentage without a stated
noise floor invites over-reading a number that's actually noise.

## 4. Memory measurement: cross-check, don't trust one instrument

Perf.md §7's `Physical footprint` vs. `Writable regions: Total` finding is
the cautionary precedent: a single memory metric (`vmmap`'s headline
figure) looked like a clean, monotonically-declining growth-rate story, and
that story was an artifact of macOS's memory compressor varying with
unrelated system load — not a fact about the process being measured. The
fix wasn't a better single metric; it was **reading two independent
instruments and trusting only what both agree on**.

This project's own precedent, already followed once: `rz_bench.rs`'s
counting-`GlobalAlloc` peak figure was cross-checked against `/usr/bin/time
-l`'s `maximum resident set size` for one nonce last session, and the two
matched almost exactly (6.27 GB counting-allocator vs. 6733873152 bytes
OS-reported) — this is what made that number trustworthy rather than merely
plausible. **Standing rule: any new peak-memory figure gets the same
two-instrument cross-check before being written into a `STATUS.md` or
baseline file as a trusted number**, not just the counting allocator alone.

## 4a. Compare equal work, and verify the comparison in the disassembly

The cautionary precedent (uniblake U1, 2026-07-16, `BLAKE/uniblake/`):
a benchmark meant to measure the cost of one variable — routing each
BLAKE2b compress through a dispatch function pointer vs. calling it
directly — first reported the dispatched path as **15% FASTER**, a
physically impossible "negative cost" for adding an indirection.

**What actually happened.** The two paths were not doing equal work.
"Path A" ran the full streaming API through the compiled library; "Path
B", labelled *inlined*, ran hand-written `static` wrapper functions in
the benchmark's own translation unit. Three confounds rode along
unnoticed: (1) B's wrappers were **not actually inlined** — the
disassembly showed them as real `t`-symbol functions, so "inlined" was
a false label; (2) the two paths copied the midstate differently
(`memcpy` vs. struct assignment); (3) the compiler allocated registers
and optimized the *loop around* each path differently. The −15% was
measuring **incidental codegen differences between two non-equivalent
implementations of the same algorithm**, not the indirection. The
dispatched side came out ahead essentially by accident of how each
side's surrounding code happened to compile.

**The fix, and the corrected result.** Rewrite so the *only* difference
between the two timed paths is the one variable under test: same call
site, same surrounding loop, same work — direct `f(x)` vs. `(*fp)(x)`
where `fp` is laundered through a `volatile` so the compiler cannot
devirtualize it (which is the real shipped situation — the pointer is
chosen at runtime by the CPU probe). Re-measured, the dispatch tax was
**+0.25 to +0.68% per compress** — a small positive cost, as physics
requires, and negligible on this workload.

**Standing rules this motivates:**

1. **Sanity-check the sign and magnitude first.** A result that
   contradicts a hard prior (adding work made it faster; a wider SIMD
   path is 3× slower with no explanation) is a **measurement bug until
   proven otherwise**, not a discovery. Investigate before recording.
2. **Isolate one variable.** Two timed paths must differ in exactly the
   thing under test and be identical in everything else (allocation,
   copy method, call-site shape, surrounding loop). If you can't state
   the single difference in one sentence, the benchmark isn't ready.
3. **Verify the comparison in the disassembly / symbol table when the
   claim depends on codegen.** "Inlined vs. not", "vectorized vs. not",
   "devirtualized vs. not" are checkable facts (`objdump`, `nm`), not
   assumptions to label by intent. The −15% would have been caught
   immediately by noticing B's wrappers were real symbols.
4. **Defeat the optimizer deliberately when modelling runtime
   behavior.** If the shipped code makes a choice at runtime (a probed
   function pointer), the benchmark must prevent the compiler from
   resolving it at compile time (`volatile`, or a value the compiler
   can't trace), or it measures a faster path than production runs.
5. **Keep the wrong result on the record.** The discarded −15% and why
   it was wrong stay written down (here and in the uniblake STATUS
   findings) — a benchmark that was believed and then corrected is more
   instructive than one that was simply right, and stops the same trap
   being re-fallen-into.

## 5. Window/parameter reporting

Perf.md §2's whole bucket-breakdown table is uninterpretable without its
exact block-height range stated alongside it — the same percentage split
means something different at different points in the chain, because
content mix (shielded-tx volume, block size) varies by height. The
project-specific analog here: any result must state the exact `(WN, WK,
RESTBITS)` and input/nonce it covers — not "the (144,5) numbers," which
elides whether it's the vendored C, the Rust port, debug or release, one
nonce or several. `SOLVER_CORPUS/rz/README.md`'s existing naming-precision
note ("(144,4)" ambiguity between RESTBITS and k) is the same discipline
applied to naming; this section is the same discipline applied to results.

## 6. Provenance query at the source, not memorized

`Req/rust`'s `req_bench.rs` doesn't stamp git revision into its JSON lines
today (checked: `report.rs`'s `to_json_line` emits `bench`/`n`/`k`/`units`/
`reps`/`min_ns`/`median_ns`/`mad_ns`/`tag`/`arch` — no git field). This is a
real gap relative to §1's own stated requirement, tracked as a follow-on
(§7 below) — not retrofitted silently into `report.rs` by this document,
since that's a decision for whoever picks up the `reqbench` extraction work.

## 7. Concrete follow-on work this document motivates

- **Extract `report.rs`'s `Record`/`BaselineEntry`/`compare`/`Verdict`
  logic into a small, dependency-free `reqbench` crate** under
  `Req/SOLVER_CORPUS/reqbench/`, generalized so the identity key isn't
  hardcoded to `(n,k)` (ports key on `(WN,WK,RESTBITS)` or similar), with
  git-provenance stamping added at extraction time. `Req/rust`'s own
  `report.rs` can then either depend on this crate or stay a documented,
  intentionally-separate sibling implementation — record which, once
  decided, rather than silently forking.
- **Rewrite `rz_bench.rs`** onto `reqbench`: repeated trials (§2), a
  provenance header per run (§1), the memory cross-check as a standing,
  automated part of its output (§4) rather than the one manual check done
  last session.
- **A template** (skeleton `bench.rs` + `ENVIRONMENT.md` + `check_env.sh`,
  generalizing what RZ built ad hoc) so RK/RT/CS start from this discipline
  rather than each reinventing it, added to `Req/SOLVER_CORPUS.md`'s
  cross-cutting requirements.

None of the above is implemented by this document itself — this is the
specification; the crate, the rewritten `rz_bench.rs`, and the template are
separate, trackable pieces of work (`Req/PLAN.md`).
