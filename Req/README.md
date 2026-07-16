# Req — Requihash miner and verifier

Reference implementation of the regularity-repaired variant of Equihash
proposed by Tang, Sun, and Gong, "On the Regularity of the Generalized Birthday
Problem" [PAPERS.md has summary and references]

Documentation map: [SPEC.md](SPEC.md) — the byte-exact family specification
(what's implemented vs. specified-only); [ARCHITECTURE.md](ARCHITECTURE.md) —
code/backend structure, plus which 2016-17 optimization technique each solver
backend implements and measures; [PLAN.md](PLAN.md) — Groups A–C status and
requirements, the cross-track sequencing view; [BENCHMARK.md](BENCHMARK.md)
— throughput measurements and harness fitness; [SIZING.md](SIZING.md) —
solution size and memory across parameters, naive vs. index-pointer;
[SECURITY_ANALYSIS.md](SECURITY_ANALYSIS.md) — structural attack-surface
review, including the time-memory-tradeoff (TMTO) test plan (§8-8a);
[SOLVER_CORPUS.md](SOLVER_CORPUS.md) — standalone historical solver/verifier
ports (RK/Khovratovich solver, RZ/tromp's pinned single-core-stripped
solver, RT/tromp's full multi-core solver, CS/Sequihash) for expertise and
cross-implementation measurement, separate from the pending Group A-C work;
[../UNIHASH.md](../UNIHASH.md) — a proposed unifying parametrization across
Equihash/Requihash/Sequihash (research, not adopted, kept separate from this
spec so it doesn't pollute pending-implementation context);
[../Dirs.md](../Dirs.md) — how the surrounding directories (ZKs reference
clones, Zebro, Zero400/ZeroPerf) relate to this work;
[../BLAKE/BLAKE.md](../BLAKE/BLAKE.md) — BLAKE-family theory, provenance,
API flavors, and this project's portability patches to third-party BLAKE2
code (the hash-primitive companion to this spec's Seam A).

## Origin and scope

This effort started from Zebro's own PoW decision (D3, `~/Work/ZK/Zebro/CONSENSUS.md`
§2.2): Zebro is a Rust rewrite migrating away from the Zcash/Zero C++ lineage,
and needed a real evidence base before picking a parameterized PoW spine. That
need drove an Equihash literature/implementation review
([../Equihash.md](../Equihash.md), [../SOLVERS.md](../SOLVERS.md)), which
surfaced both the regularity mis-specification the 2025 paper repairs
(Requihash) and concrete portability/optimization gaps in existing solvers —
this directory is the working implementation that resulted, feeding Zebro's
M3 evidence package back. It is a research and reference-implementation
effort, not a production node: the actual Zero Currency production chain
(release candidate 4.0.1, incremental C++, stock Equihash, not Rust/Zebra-code-compatible)
lives in `Zero400`/`ZeroPerf`, tracked separately — see [../Dirs.md](../Dirs.md).

This directory delivers two independent implementations that share a byte-exact
wire format so a solution mined by one verifies in the other:

- `cpp/` — C++17, following the zcash `src/crypto/equihash` conventions (BLAKE2b
  personalization, minimal/compressed solution encoding, distinct-index and
  ordering checks). Standalone: bundles its own BLAKE2b, no zcash build coupling.
- `rust/` — Rust, following the zebra `zebra-chain/src/work/equihash.rs` verifier
  conventions (a `check`-style validator plus a solver for round-trip tests).

## Project direction and history

Three stages, in order: (1) the Equihash literature/implementation review
([../Equihash.md](../Equihash.md) — history, the 2016-17 ASIC-defeat
mechanism, the 2025 theory; [../SOLVERS.md](../SOLVERS.md) — primary-source
solver history: the original authors' reference implementation, xenoncat's
index-pointer derivative, tromp's full commit history and its integration
into zcashd); (2) the Requihash repair this review surfaced, specified and
cross-validated here (§"What Requihash changes" below); (3) the current,
ongoing work — implementation quality, concurrency, and memory-sizing fitness
across parameters, which superseded further hash-vs-hash comparison as the
priority once the ARM blake2b/blake3 campaign answered that question
([PLAN.md](PLAN.md) "Current direction"). The single biggest open item across all
of Req/ is compact index-pointer storage (`Req/PLAN.md` A6) — it unblocks
(200,9)-scale mining, gives an honest memory floor, and is the prerequisite
for the TMTO experiments in `SECURITY_ANALYSIS.md` §8. [PLAN.md](PLAN.md) is
the authoritative, continuously-updated status tracker; nothing below should
be trusted over it for "what's done vs. not."

## How to read this documentation

Pick the entry point that matches what you're doing, not the file that sounds
closest to your question — several documents cover adjacent ground and each
has one clear owner (cross-references below say who).

- **First time here, want the shape of the whole thing.** Read this README
  top to bottom (10 minutes), then skim [../Equihash.md](../Equihash.md)'s
  Findings section (F-A1-F-A11) for the research background. Everything else
  is reference material to dip into once you have a specific question.
- **Tracking design, spec, or status.** [SPEC.md](SPEC.md) is the frozen
  normative wire format (what's implemented vs. specified-only, per
  configuration point). [PLAN.md](PLAN.md) is the live, broader work tracker
  (all groups, all in-flight items) — cite SPEC for "what the format is,"
  cite PLAN for "what's being worked on and by when."
- **Reviewing results or measured conclusions.** [BENCHMARK.md](BENCHMARK.md)
  for throughput/timing; [SIZING.md](SIZING.md) for memory/solution-size data
  across parameter sweeps (including the paper's own published figures and a
  documented correction trail). Both are evidence-graded (Measured/Reported/
  Structural/Hypothesis) — check the grade before citing a number elsewhere.
- **Investigating, debugging, or optimizing a specific solver/hash backend.**
  [ARCHITECTURE.md](ARCHITECTURE.md) — the seam/trait structure, directory
  layout, and (§7) exactly which 2016-17 optimization technique each backend
  implements, with measured before/after numbers for each.
- **Doing security/adversarial review, or picking up a TMTO/hypothesis
  experiment.** [SECURITY_ANALYSIS.md](SECURITY_ANALYSIS.md) end to end — the
  shortcut hunt, the lessons (L1-L8) and hypotheses (H1-H5), and the
  step-wise experiment plan (§8) with its counting methodology (§8a). Start
  at the critical path note (1→2→3) rather than reading the whole file if
  you're picking up implementation work, not doing analysis.
- **Understanding why Requihash exists at all, or the wider industry
  context.** [../Equihash.md](../Equihash.md) (history, defeat, 2025 theory,
  findings) and [../SOLVERS.md](../SOLVERS.md) (primary-source solver
  history) — both live one level up in `Requihash/`, since they're about the
  problem and the field, not this specific codebase.
- **Figuring out how this relates to Zebro, ZKs, or Zero400/ZeroPerf.**
  [../Dirs.md](../Dirs.md) — the directory map, including which surrounding
  repos are safe to read-and-cite versus edit.

## What Requihash changes

Equihash draws all `2^K` solution indices from a single list, where item `j` is

    BLAKE2b_person(input || nonce || j)

Requihash draws index `i` (0-based, `i` in `0..2^K`) from list-class `i mod K`,
so the generator becomes

    BLAKE2b_person(input || nonce || (i mod K) || j)

This is the sequential regularity constraint `x_i` drawn from list `i-1 mod K` of
the paper. It converts the loose single-list GBP that Equihash actually solves
(LGBP) back into the regular k-list GBP (RGBP) Wagner's algorithm was designed
for, which structurally disables the index-pointer single-list optimization that
collapsed Equihash's ASIC resistance. Everything else — the binary-tree collision
merge over `ell = N/(K+1)` bits per round, XOR-to-zero at the root, distinct
indices, canonical ordering — is unchanged from Equihash.

Note K here is the tree depth (Wagner's `k = log2(K_lists)`); we follow the
Equihash `(n, k)` convention where the solution has `2^k` indices, matching the
paper's `(n, K = 2^k)` table. The list-class modulus is `k` (the number of tree
layers plus one gives the list count in the strict formulation; we use `i mod k`
as the concrete regularity binding, documented in `cpp/requihash.h`).

## Build and test

    cpp/   : cmake -S cpp -B cpp/build && cmake --build cpp/build && cpp/build/req_test
    rust/  : cargo test --manifest-path rust/Cargo.toml
    cross  : cpp/build/req_gen vectors   (writes vectors/*.json)
             cargo run --manifest-path rust/Cargo.toml --bin req_xcheck -- vectors

### Rust/Cargo topology — no workspace, each crate stands alone

There is **no Cargo workspace** anywhere under `Req/` — no top-level
`Cargo.toml` with a `[workspace]` section, no single `cargo build` that
sweeps everything. This is deliberate, not an oversight: `SOLVER_CORPUS.md`'s
own cross-cutting requirement is that each port needs "no other context
needed from this repository's other documents," and a shared workspace
would quietly couple every port's dependency resolution and lockfile to
every other's. Concretely, five independent Rust packages exist today,
each with its own `Cargo.toml`/`Cargo.lock`/`target/` (CS is a separate,
CMake-built C++ crate, not Cargo — see `SOLVER_CORPUS/cs/README.md`):

| Crate | Path | Depends on (path deps) |
|---|---|---|
| `requihash` | `rust/` | none — only crates.io deps |
| `reqbench` | `SOLVER_CORPUS/reqbench/` | none — dependency-free by design (`BENCH.md`) |
| `rz` | `SOLVER_CORPUS/rz/` | `reqbench` (relative path `../reqbench`) |
| `rk` | `SOLVER_CORPUS/rk/` | `reqbench` (relative path `../reqbench`) |
| *(future)* `rt` | `SOLVER_CORPUS/rt/` | will depend on `reqbench` the same way `rz`/`rk` do, per `SOLVER_CORPUS/_template/` |

**Expected usage: `cd` into the crate you want, then plain `cargo`
commands** — never `--manifest-path` from `Req/`'s own root for anything
under `SOLVER_CORPUS/` (that flag is only shown above for `rust/`, which
predates `SOLVER_CORPUS/` and is `Req/README.md`'s own existing
convention; either form works for a single-crate command, `cd` is just
less error-prone once path-dependent crates are involved):

    cd Req/SOLVER_CORPUS/reqbench && cargo test
    cd Req/SOLVER_CORPUS/rz        && cargo test
    cd Req/SOLVER_CORPUS/rz        && cargo run --release --bin rz_bench -- --json baselines/<tag>.jsonl --tag <tag>

`rz`'s relative path dependency (`../reqbench`) resolves purely from disk
layout — no workspace file needed for `cargo build`/`cargo test` inside
`rz/` to find `reqbench` — but it does mean **`reqbench/` and `rz/` must
stay siblings under `SOLVER_CORPUS/`**; moving one without the other
breaks the path. `SOLVER_CORPUS/_template/` has **no `Cargo.toml` at
all**, deliberately, so it can never be built or accidentally swept by any
future workspace-wide command — copy its contents into a new port
directory first, then add the `Cargo.toml.snippet` pieces into that port's
own manifest.

**No workspace is planned.** If ports under `SOLVER_CORPUS/` multiply and
duplicate build/CI overhead becomes a real cost, revisit this — but do not
add a workspace preemptively; it works against the standalone-per-port
design `SOLVER_CORPUS.md` states explicitly.

## Verified results

Both implementations solve and verify at (48,5) and (72,5), and cross-validate: a
solution mined by the C++ miner, serialized to its minimal wire form, is decoded
and verified by the independent Rust verifier (byte-exact BLAKE2b, leaf keying,
and encoding).

- C++ `req_test`: BLAKE2b-512 known-answer; param rejection; solve+verify (48,5)
  and (72,5); minimal-encoding round trip; Table 3 wire sizes at (200,9).
- Rust `cargo test`: same coverage plus a regularity check (a swapped-leaf
  variant of a valid solution is rejected).
- Cross-check: Rust verifies both C++ vectors — `CROSS-CHECK PASS (2 vectors)`.

Paper Table 3 wire sizes are confirmed at Zcash production params (200,9):
Equihash-compatible encoding = 1344 bytes, Requihash compact encoding = 1280
bytes (the sequential constraint removes one disambiguation bit per index).

## Backends (mix-and-match seams)

The code is structured around two swap seams plus a separate verifier seam (see
[ARCHITECTURE.md](ARCHITECTURE.md)); every tier has working, cross-validated
examples in both languages.

| Seam | Backends (Rust) | Backends (C++) |
|---|---|---|
| Hash (A) | `scalar` (bundled BLAKE2b), `simd` (blake2b_simd, feature-gated) | scalar (bundled) |
| Solve (B) | `reference`, `arena`, `bucket` (2016-17 incomplete sort), `parallel` (rayon, feature-gated) | `Solve` (reference), `SolveArena` |
| Verify | `reference`, `arena`, `early` | `IsValidSolution` (reference), `IsValidSolutionEarly` |

All backends are proven equivalent: `all_solvers_agree`, `all_verifiers_agree`,
`arena_matches_reference`, and the SIMD hasher passes the self-test gate
(`simd_hasher_matches_scalar`) that autodetect requires before adopting any
accelerated backend. Build the Rust accelerated tiers with
`--features rayon,simd`.

## Security analysis

[SECURITY_ANALYSIS.md](SECURITY_ANALYSIS.md) is a structural, adversarial analysis:
the shortcut hunt (precomputation, parallelization, memory-reduction, and the novel
regularity-structure surface), why the search pattern is what let the 2016-17
optimizations cut memory many-fold (the layouts themselves are in
[ARCHITECTURE.md](ARCHITECTURE.md) §7), how block contents bind to the PoW, and eight
classified lessons applied methodically across Requihash construction variants. It
carries five hypotheses (H1-H5, led by class-selective TMTO), a step-wise experiment
plan, and the TMTO counting methodology (§8-8a). Core patterns are illustrated in
`figures/` (memory collapse, Wagner tree with attack surfaces, block binding).

## Performance

See [BENCHMARK.md](BENCHMARK.md) for measured numbers, a profile, and the round-2
all-backend comparison. Headline:
solve time is dominated by the merge (76-87%), and inside the merge by heap
allocation (59% of samples), not by BLAKE2b (17%) — so the first optimization is
arena allocation in the solver, not SIMD hashing. The verifier is a flat ~7 us
(~140k/s) and needs no acceleration. See [ARCHITECTURE.md](ARCHITECTURE.md) for
the mix-and-match backend structure this motivates.

## Why not mine (200,9) here

At (200,9) the initial list is `2^(ell+1) = 2^21` leaves and a real solve holds
the whole Wagner tree; the basic (correctness-oriented) solver in `solver.h` is
not memory-optimised and is unsuitable for production parameters. The parameter
arithmetic, personalization, and encoding are exercised at (200,9) via the
Table 3 size checks; the solve/verify round trip is exercised at the smaller
`(48,5)` and `(72,5)` sets. A production miner would port the index-pointer-free
k-list solver (paper Prop. 3) — deliberately, since disabling the single-list
index-pointer optimisation is the whole point of Requihash (Equihash.md F-A4).

## Key implementation note

The regularity constraint lives in one place in each implementation:
`GenerateHash` / `leaf_row`, which keys leaf `i` by `(i mod k, i / k)` — list
class `i mod k` is the regularity binding, `i / k` the intra-class counter that
keeps every leaf distinct. Removing the `i mod k` term recovers single-list
Equihash. This is the minimal, client-side-only change the paper describes.
