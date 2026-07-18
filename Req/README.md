# Req — the Requihash implementation (Rust, with a C++ differential twin)

The evolving Rust implementation of Requihash — the regularity-repaired
Equihash of Tang, Sun, and Gong ([../PAPERS.md](../PAPERS.md)) — built to
zebra conventions (`zebra-chain/src/work/equihash.rs`: a `check`-style
validator plus a solver for round-trip tests) so it can slot in as a drop-in
at the verifier seam of **Zebro**, the Rust revamp of the Zcash/Zebra
lineage, while carrying the curve-jumping innovation Zebro's PoW decision
(D3) needs evidence for: the k-list regularity repair itself, hash-flavor
substitution (BLAKE2b/BLAKE3 at Seam A), and the m-dial parametrization of
the whole GBP family (`PoW(n, k, hash, m, keying, context)`, [SPEC.md](SPEC.md)).

A byte-exact C++ implementation (`cpp/`, zcashd `src/crypto/equihash`
conventions, bundled BLAKE2b, no zcash build coupling) serves as the
differential oracle: the two share the wire format exactly, so a solution
mined by one verifies in the other. Project-level orientation, the research
background, and the BLAKE and solver-corpus tracks live one level up —
[../README.md](../README.md).

## Documentation map (Req-owned documents)

[SPEC.md](SPEC.md) — the byte-exact family specification (what's implemented
vs. specified-only); [PLAN.md](PLAN.md) — the live work tracker (topics
T1–T7; trust it over any other doc for status); [ARCHITECTURE.md](ARCHITECTURE.md)
— code/backend structure, plus which 2016-17 optimization technique each
solver backend implements and measures (§7); [BENCHMARK.md](BENCHMARK.md) —
throughput measurements and harness fitness; [SIZING.md](SIZING.md) —
solution size and memory across parameters, naive vs. index-pointer, with
the valid-parameter bounds table; [SECURITY_ANALYSIS.md](SECURITY_ANALYSIS.md)
— structural attack-surface review and the TMTO test plan (§8-8a);
[SOLVER_CORPUS.md](SOLVER_CORPUS.md) — the standalone historical solver
ports (RZ/RK/RT/CS); [REVIEW_REQ.md](REVIEW_REQ.md) — the
implementation-quality review record (findings F1–F14, corner-case
inventory); [BENCH.md](BENCH.md) — the shared measurement discipline.
Cite SPEC for "what the format is", PLAN for "what's being worked on".

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
    bench  : rust/bench.sh   (standard reference-machine series, BENCH.md discipline)

### Rust/Cargo topology — no workspace, each crate stands alone

There is **no Cargo workspace** anywhere under `Req/` — no top-level
`Cargo.toml` with a `[workspace]` section, no single `cargo build` that
sweeps everything. This is deliberate, not an oversight: `SOLVER_CORPUS.md`'s
own cross-cutting requirement is that each port needs "no other context
needed from this repository's other documents," and a shared workspace
would quietly couple every port's dependency resolution and lockfile to
every other's. Concretely, **six** independent Rust packages exist today,
each with its own `Cargo.toml`/`Cargo.lock`/`target/`. (CS has a
CMake-built C++ port at `SOLVER_CORPUS/cs/` **and** a Cargo Rust re-port
at `SOLVER_CORPUS/cs-rs/`, added 2026-07-17 — the C++ is the Rust's
differential oracle.)

| Crate | Path | Depends on (path deps) |
|---|---|---|
| `requihash` | `rust/` | none — only crates.io deps |
| `reqbench` | `SOLVER_CORPUS/reqbench/` | none — dependency-free by design (`BENCH.md`) |
| `rz` | `SOLVER_CORPUS/rz/` | `reqbench` (relative path `../reqbench`) |
| `rk` | `SOLVER_CORPUS/rk/` | `reqbench` (relative path `../reqbench`) |
| `cs-rs` | `SOLVER_CORPUS/cs-rs/` | none — `blake2b_simd` only (no `reqbench` bench binary yet — a tracked gap) |
| *(future)* `rt` | `SOLVER_CORPUS/rt/` | will depend on `reqbench` the same way `rz`/`rk` do, per `SOLVER_CORPUS/_template/` |

**Expected usage: `cd` into the crate you want, then plain `cargo`
commands** — never `--manifest-path` from `Req/`'s own root for anything
under `SOLVER_CORPUS/` (that flag is only shown above for `rust/`, which
predates `SOLVER_CORPUS/` and is this file's own existing convention;
either form works for a single-crate command, `cd` is just less
error-prone once path-dependent crates are involved):

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

- C++ `req_test`: BLAKE2b-512 known-answer; parameter-bound rejection
  (F12–F14) and `NBounds` exhaustive agreement; solve+verify (48,5) and
  (72,5); minimal-encoding round trip; the full corner-case matrix
  (near-misses, out-of-range indices, per-round collision/ordering tampers);
  Table 3 wire sizes at (200,9).
- Rust `cargo test`: same coverage plus a regularity check (a swapped-leaf
  variant of a valid solution is rejected), the exact-`Error`-variant
  rejection-path matrix across all three verifiers, and `n_bounds`/`valid_n`
  exhaustive agreement with the constructor.
- Cross-check: Rust verifies both C++ vectors plus the 46 official Zcash
  Equihash KATs (routed by keying) — `CROSS-CHECK PASS`.

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
| Solve (B) | `reference`, `arena`, `bucket` (2016-17 incomplete sort), `parallel` (rayon, feature-gated), `pointer` (prototype, unregistered — PLAN T2.4) | `Solve` (reference), `SolveArena` |
| Verify | `reference`, `arena`, `early` | `IsValidSolution` (reference), `IsValidSolutionEarly` |

All registered backends are proven equivalent: `all_solvers_agree`,
`all_verifiers_agree`, `arena_matches_reference`, and the SIMD hasher passes
the self-test gate (`simd_hasher_matches_scalar`) that autodetect requires
before adopting any accelerated backend. Build the Rust accelerated tiers with
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
the mix-and-match backend structure this motivates. The single biggest open
implementation item is the production index-pointer backend
([PLAN.md](PLAN.md) T2.4) — it unblocks (200,9)-scale mining, gives an honest
memory floor, and is the prerequisite for the TMTO experiments.

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

The regularity constraint lives in one place in each implementation — C++:
`GenerateHash`; Rust: `leaf_row_into` (the allocation-free carrier of the
binding; `leaf_row` and every hot leaf-fill loop route through it, per
[REVIEW_REQ.md](REVIEW_REQ.md) F1). It keys leaf `i` by `(i mod k, i / k)` —
list class `i mod k` is the regularity binding, `i / k` the intra-class
counter that keeps every leaf distinct. Removing the `i mod k` term recovers
single-list Equihash. This is the minimal, client-side-only change the paper
describes.
