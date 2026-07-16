# RZ — native Rust port of the vendored tromp Equihash solver

## What this targets

This crate ports the **solve-side** algorithm in
`equihash-0.3.0/tromp/equi_miner.c` (the pinned crate's vendored,
single-core-stripped copy of John Tromp's Equihash solver) to native
Rust, driven single-core exactly as the crate's own FFI wrapper
(`equihash-0.3.0/src/tromp.rs::worker`) drives the C functions:
hardcoded `id=0`, no thread spawn, no call into the C file's own unused
`worker()`.

This **is** the actual algorithmic implementation reached by Zebra's
proof-of-work check via its `zebra-chain::work::equihash::Solution`
type, which is a thin wrapper delegating directly to
`is_valid_solution`/`tromp::solve_200_9` with no independent algorithm
of its own. Provenance, with exact commit hashes (see
`~/Work/ZK/Requihash/SOLVERS.md` §5 for full detail):

- **2024-01-04**, Jack Grigg re-imports zcashd's frozen copy of tromp's
  solver into `zcash/librustzcash` (`45652a21a`), converting it to
  compile as plain C.
- **2024-01-11**, teor removes the multi-threading entirely
  (`b737d0fe2`, "Remove unused thread support to enable Windows
  compilation") — this is what makes `equi_miner.c`'s own `worker()`
  (never called by this port or by `tromp.rs`) genuinely single-core:
  it contains no `pthread_barrier_t` or any other synchronization
  primitive.

## What this does NOT target

The `equihash` crate also ships a **separate, independent** pure-Rust
verifier (`equihash-0.3.0/src/verify.rs` and `src/minimal.rs`) with zero
references to `tromp`/`extern`/`unsafe`. That verifier is out of scope
for this port entirely — it's a different algorithm (tree-fold
verification, not Wagner's-algorithm-style bucket solving) and is
already covered by `Req/rust/src/verify/`'s own cross-validated
backends elsewhere in this corpus.

## Scope of this pass: (144, 5, 4) only

**This pass ports and validates `(WN=144, WK=5, RESTBITS=4)` only.**
The vendored `equi_miner.c` also compiles at `(WN=200, WK=9,
RESTBITS=8)` and `(WN=200, WK=9, RESTBITS=9)` — both confirmed
buildable by this crate's `build.rs` (all three cross-check C binaries
build successfully) — but **porting those two parameter sets to Rust is
explicitly not done in this pass.** This restriction is deliberate: a
prior attempt spread effort across multiple hard parameter sets in
parallel and finished none of them; this pass proves the port at the
smallest, cheapest parameter set first and stops there. Extending
`src/lib.rs` to cover `(200,8)`/`(200,9)` is follow-on work.

Note that the crate's `#if`/`#elif` branches also nominally reference
`(WN=144, RESTBITS=4)`'s `BUCKBITS==20` xorbucketid formula and a
`(WN=96, RESTBITS=4)` branch in a couple of spots, but only the three
`(WN,WK,RESTBITS)` triples above are exercised end-to-end (getxhash +
bucketid + xorbucketid branches all present and consistent) — see
`STATUS.md` for the full branch-by-branch derivation at (144,5,4).

## Environment

Package/build/link prerequisites (compiler, the vendored crate's source
location, the reference BLAKE2b clone) are captured in
[ENVIRONMENT.md](ENVIRONMENT.md) — including the one genuinely fragile,
hardcoded-local-path dependency. Run `./check_env.sh` before `cargo test`
to check all prerequisites in one step.

## Layout

- `src/lib.rs` — the ported algorithm, `(WN=144, WK=5, RESTBITS=4)`
  hardcoded as `const`s (no const generics / multi-parameter generality
  in this pass — see Scope above). No I/O, no CLI. Entry point:
  `pub fn solve_144_4(input: &[u8], nonce: &[u8]) -> Vec<Vec<u32>>`,
  returning the raw index-set solutions found (each `Vec<u32>` of length
  32), mirroring `tromp.rs::worker`'s return type before the crate's
  separate `minimal_from_indices` compression step.
- `src/bin/rz_gen.rs` — CLI driver: `rz_gen <input_hex> <nonce_hex>`
  prints one `{"indices":[...]}` JSON line per solution, matching the
  cross-check C binary's output shape exactly.
- `build.rs` — builds three cross-check binaries
  (`rz_xcheck_144_4`, `rz_xcheck_200_8`, `rz_xcheck_200_9`) by compiling
  the vendored, unmodified `equi_miner.c` against a glue-code BLAKE2b
  wrapper (`cross_check_c/`) isolating it from the reference BLAKE2b
  implementation vendored at `BLAKE/vendor/blake2/` (repo-relative;
  `RZ_BLAKE2_REF_DIR` overrides). Only the
  `_144_4` binary is exercised by this pass's tests.
- `cross_check_c/` — `harness_main.c`, `blake2b_glue.c/h`: harness code
  written for this port (not vendored/pinned source), reproducing
  `tromp.rs::worker`'s exact driver sequence in C so the cross-check
  binaries are behaviorally identical to what `equihash::tromp::solve_200_9`
  would do at each parameter set.
- `tests/cross_check.rs` — runs both the Rust port and
  `rz_xcheck_144_4` on 3 distinct nonces, asserts the returned index
  *sets* match exactly.
- `src/bin/rz_bench.rs` — measures `solve_144_4`'s wall time (7 repeated
  trials, min/median/MAD, not a single sample) and peak memory (counting
  allocator, automatically cross-checked against OS RSS each run), stamped
  with git commit/dirty-tree provenance, via the shared `reqbench` crate
  (`../reqbench/`, `Req/BENCH.md`). `cargo run --release --bin rz_bench --
  --json baselines/<tag>.jsonl --tag <tag>` appends a baseline record;
  `-- --baseline baselines/<tag>.jsonl --tag <tag>` compares a new run
  against it (Win/Regression/Noise/New per `Req/BENCH.md` §3's decision
  rule). See STATUS.md step 6 for the measured numbers and a direct
  comparison against the C original's own peak memory.

## Validation status

`cargo test` passes for `(144, 5, 4)` across 3 distinct nonces (all
sharing one 64-byte repeated-byte-pattern input, nonces differing):
an all-zero 28-byte nonce, a nonce of all `0x01` bytes, and a nonce
ending in `0x2a`. All three produced byte-identical (release build) or
set-identical (as compared by the test) index sets between the Rust
port and the vendored C cross-check binary — see `STATUS.md` for the
exact solutions and timings recorded at each step.

Target achieved: **raw index set**, byte-exact (order-identical too, in
the one case manually diffed line-for-line). The compressed-pair wire
form (`minimal_from_indices` in the crate) was not attempted in this
pass — out of scope per the task's step list, which stops at the raw
index-set JSON shape matching `rz_xcheck_144_4`.

## Not done in this pass (follow-on work)

- `(WN=200, WK=9, RESTBITS=8)` and `(WN=200, WK=9, RESTBITS=9)` — cross-
  check C binaries already build successfully via the existing
  `build.rs`, but no Rust port exists for either yet.
- Compressed-pair (minimal encoding) solution output — only the raw
  index set is validated.
- Any performance work — this port allocates its bucket storage as
  plain `Vec`s rather than replicating the C's arena-reuse memory
  layout (safe because the algorithm is strictly round-sequential; see
  `STATUS.md`'s "Storage layout note"), and has not been profiled or
  tuned beyond "passes tests in reasonable time."
