# Req — Requihash miner and verifier

Reference implementation of Requihash, the regularity-repaired variant of Equihash
proposed by Tang, Sun, and Gong, "On the Regularity of the Generalized Birthday
Problem" ([eprint 2025/1351](https://eprint.iacr.org/2025/1351)), Section 5.2.
Findings context: [../Equihash.md](../Equihash.md), F-A4.

Documentation map: [SPEC.md](SPEC.md) — the byte-exact family specification
(what's implemented vs. specified-only); [PLAN.md](PLAN.md) — Groups A–C
status and requirements, the cross-track sequencing view; [BENCHMARK.md](BENCHMARK.md)
— throughput measurements and harness fitness; [SIZING.md](SIZING.md) —
solution size and memory across parameters, naive vs. index-pointer; [TMTO.md](TMTO.md)
— the time-memory tradeoff research plan; [SECURITY_ANALYSIS.md](SECURITY_ANALYSIS.md)
— structural attack-surface review.

This directory delivers two independent implementations that share a byte-exact
wire format so a solution mined by one verifies in the other:

- `cpp/` — C++17, following the zcash `src/crypto/equihash` conventions (BLAKE2b
  personalization, minimal/compressed solution encoding, distinct-index and
  ordering checks). Standalone: bundles its own BLAKE2b, no zcash build coupling.
- `rust/` — Rust, following the zebra `zebra-chain/src/work/equihash.rs` verifier
  conventions (a `check`-style validator plus a solver for round-trip tests).

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
regularity-structure surface), the expected-vs-achieved memory layout that explains
Equihash's many-fold reduction, how block contents bind to the PoW, and eight
classified lessons applied methodically across Requihash construction variants. It
carries five hypotheses (H1-H5, led by class-selective TMTO) and a step-wise
experiment plan. Core patterns are illustrated in `figures/` (memory collapse,
Wagner tree with attack surfaces, block binding). See also [TMTO.md](TMTO.md).

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
