# RK — native Rust port of Khovratovich's original Equihash reference solver

## What this targets

This crate ports Dmitry Khovratovich's original CC0-licensed C++11
Equihash reference solver — `equihash-khovratovich/Source/C++11/pow.h`
and `pow.cc` (117 + 218 lines, [github.com/khovratovich/equihash](https://github.com/khovratovich/equihash),
local clone `~/Work/ZK/ZKs/equihash-khovratovich`) — to native Rust. This
is the historical artifact itself (2016, CC0, predates the whole
2016-17 optimization wave `Equihash.md` §2 covers), not a from-scratch
naive solver: `Req/rust/src/solve/reference.rs` already independently
implements a naive Wagner walk for this project's own use, so RK's value
is specifically a faithful line-for-line port of Khovratovich's own code,
not another implementation of the same idea.

The `Equihash` class's five methods are preserved with their original
names and control flow: `initialize_memory` (`InitializeMemory`),
`fill_memory` (`FillMemory`), `resolve_collisions`
(`ResolveCollisions`), `find_proof` (`FindProof`), and the two-part tree
reconstruction `resolve_tree`/`resolve_tree_by_level`
(`ResolveTree`/`ResolveTreeByLevel`). `pow.cc`'s `Proof::Test` becomes
`Proof::test`. The port's own module (`src/lib.rs`) has no CLI/console
dependency — `src/bin/rk_gen.rs` is a separate driver.

## Parameter range: broader than RT, by construction

**This port's parameter range is `n` up to 32 bytes (256 bits), any `k`
for which `LIST_LENGTH`/memory hold** — the original is a generic
recursive tree-fold over dynamically-sized `Vec<Vec<Tuple>>`, not
bucket-specialized C with compile-time `#if`/`#elif` branches per
`(WN,RESTBITS)` pair. This is **broader than the RT port's range**: RT
(tromp's solver) is compile-time-restricted to specific parameter pairs;
RK has no such restriction. It is easy to assume the "older" 2016
reference is more limited than tromp's later, more-optimized solver —
the opposite is true for parameter *generality* (RT is more limited;
it is far more *optimized* at the parameters it does support, which is
the actual point of the later work).

Reaching Zero Currency's mainnet `(192,7)` or Zcash's `(200,9)` needs
**no port code change at all** — only running the existing solver at
those parameters, which this pass deliberately does not do (see
"Measured scaling" below for why).

## Original's recommended parameter families

Per the original repo's own `README.md`:

- Cryptocurrency: `(100,4)`, `(110,4)`, `(120,4)`, `(108,5)`, `(114,5)`,
  `(120,5)`, `(126,5)`
- Client puzzle: `(60,4)`, `(70,4)`, `(80,4)`, `(90,4)`, `(90,5)`,
  `(96,5)`, `(102,5)`

## Build environment — the original bundle does not build on arm64

The original repo's own `Makefile` (`g++ -m64 -maes -mavx -O3 -std=c++11
pow.cc pow-test.cc blake/blake2b.cpp -o equihash`) does not build on
Apple Silicon: `-maes`/`-mavx` are unsupported target options on
`arm64-apple-darwin`, and the bundled `blake/blake2b.cpp` implements
BLAKE2b with SSE2/SSE4.1 `__m128i` intrinsics unconditionally
(`#include <immintrin.h>`), an x86-only code path with no ARM branch.
Separately, the bundled `blake/blake2.h` itself fails to compile under
current clang's stricter ABI checking (`blake2sp_state`/`blake2bp_state`
place a `#pragma pack(push,1)`-packed array of a 64-byte-`ALIGN`ed
element type whose size isn't a multiple of 64 — a preexisting bug in
the bundled header, unrelated to AVX/SSE, and irrelevant to this port
since `pow.cc` never calls `blake2sp`/`blake2bp`). `pow.cc` also calls a
hand-written `rdtsc()` with inline x86 asm (`__asm__ __volatile__("rdtsc"...)`)
for cycle-count profiling printf output, with `#error "Not implemented!"`
on any non-x86 target.

**Fix applied when building the original for vector generation** (not
part of this Rust port, which has no such dependency at build time —
see "Validation" below): keep `pow.h`/`pow.cc` untouched except for
`rdtsc()`, which gets a portable `std::chrono::steady_clock`-based
fallback branch (algorithmically inert — it only affects the printf
timing stats the original prints, not the search itself); replace the
*included* `blake/blake2.h` with a 20-line stub declaring only the one
function `pow.cc` actually calls (`blake2b`, the simple one-shot API,
avoiding the packed-struct declarations entirely); and link against
the repository's vendored copy `BLAKE/vendor/blake2/blake2b-ref.c`
(Samuel Neves, CC0,
portable C, no intrinsics) through a small glue translation unit that
presents `pow.cc`'s exact expected signature
(`blake2b(out,in,key,outlen,inlen,keylen)` — note the argument order
differs from the reference header's own `blake2b(out,outlen,in,inlen,key,keylen)`)
and forwards to the reference implementation. This mirrors the
isolation pattern `../rz/cross_check_c/blake2b_glue.c` already
established for the same underlying problem (bundled vendored BLAKE2b
world vs. the reference implementation, kept out of the same
translation unit) — the same fix generalizes here.

No x86-specific library was substituted for another x86-specific one;
the portable reference C implementation is architecture-neutral and
this same approach would work unchanged on x86_64 too.

## Measured scaling — why `(192,7)`/`(200,9)` are out of scope for this pass

The original's `tupleList` is sized `2^(n/(k+1))` rows of `LIST_LENGTH=5`
`Tuple`s each; the algorithm has no memory-reduction techniques at all
(no index-pointer compression, no incomplete bucket sort — it predates
the entire 2016-17 optimization wave `Equihash.md` §2/§3 covers). Direct
measurement of the built C++ original on this machine (arm64, 48GB RAM),
across the full recommended `k∈{4,5}` families:

| (n,k) | tuple_n = 2^(n/(k+1)) | wall time | peak RSS | bytes/tuple_n |
|---|---|---|---|---|
| (60,4) | 4,096 | 0.00s | 4 MB | 977 |
| (70,4) | 16,384 | 0.01s | 11 MB | 671 |
| (80,4) | 65,536 | 0.06s | 42 MB | 641 |
| (90,4) | 262,144 | 0.63s | 166 MB | 633 |
| (100,4) | 1,048,576 | 4.45s | 661 MB | 630 |
| (110,4) | 4,194,304 | 12.65s | 2,640 MB | 629 |
| (120,4) | 16,777,216 | 162.00s | 10,557 MB | 629 |
| (90,5) | 32,768 | 0.11s | 27 MB | 824 |
| (96,5) | 65,536 | 0.07s | 54 MB | 824 |
| (102,5) | 131,072 | 0.33s | 107 MB | 816 |
| (108,5) | 262,144 | 0.74s | 212 MB | 809 |
| (114,5) | 524,288 | 3.22s | 427 MB | 814 |
| (120,5) | 1,048,576 | 13.87s | 846 MB | 807 |
| (126,5) | 2,097,152 | 10.81s | 1,690 MB | 806 |

The bytes/tuple_n ratio converges cleanly per `k` (≈629 B at k=4, ≈807-824
B at k=5) — confirms the scaling is exactly `O(tuple_n · k · LIST_LENGTH)`
as the algorithm's own structure predicts, no surprises. Extrapolating:
`(192,7)` has `tuple_n = 2^24 = 16.7M`, the same order as `(120,4)`'s
measured row (162s, 10.5 GB) — likely similar or worse given k=7's larger
per-tuple footprint. `(200,9)` has a smaller `tuple_n = 2^20`, but `k=9`'s
`O(k)` per-tuple cost and the `ResolveCollisions` step's `O(filled²)`
inner loop (up to `LIST_LENGTH²=25` pair-checks per bucket, `k` rounds)
make it a real, not-yet-measured unknown, not assumed-safe.

**Per direct instruction, this pass does not attempt either point** —
timing/memory calibration was done exclusively at the small, cheap `k∈{4,5}`
families above, confirming the formula before any large run, rather than
guessing at production parameters. Attempting `(192,7)`/`(200,9)` with
this specific solver (no index-pointer compression, no bucket sort) is a
suggested follow-on only with an explicit time/memory budget agreed in
advance — see "Suggestions for further investigation" below.

## Validation

Vectors, not a live subprocess dependency — matching the task's
requirement. The original C++ (with the portable-BLAKE2b fix above) was
built once in a scratch location, used to generate 8 KAT vectors covering
both recommended parameter families (`vectors/*.json`: `n60_k4_s1`,
`n70_k4_s2`, `n90_k5_s3`, `n96_k5_s4`, `n100_k4_s11`, `n108_k5_s22`,
`n120_k4_s33`, `n126_k5_s44` — includes the two exact points the task
specifies, `(100,4)` and `(108,5)`/`(126,5)`, plus `(120,4)`), then
discarded; the vectors are the only artifact carried forward. Each vector
records `{n, k, seed, nonce, indices, verified}` — `nonce` is the value
the original's own `FindProof` search landed on (it retries nonces from 1
upward until a duplicate-free solution is found), `indices` the raw
2^k-length index array (no minimal/compressed wire encoding — **the
original defines none**, consistent with `SOLVER_CORPUS.md`'s statement
that RK's byte-exact target is the index set only), and `verified` the
original's own `Proof::Test()` self-check result (all `true`).

`tests/cross_check.rs` loads every vector file, re-runs the Rust port's
`Equihash::new(n, k, Seed::from_u32(seed)).find_proof()` (replicating the
original's exact multi-nonce retry search, not a single fixed-nonce
call), and asserts **both** the returned nonce and the full index array
match the vector exactly (order-identical — the original does not sort
before returning; only its internal duplicate-check sorts a throwaway
copy). All 8 vectors pass. `(192,7)`/`(200,9)` are not vectored in this
pass (see "Measured scaling" above).

Run: `cargo test` (debug build; the cross-check test takes ~3 minutes in
debug due to `(126,5)`'s search cost — run `cargo test --release` for a
much faster pass if iterating).

## No wire/minimal encoding claim

The original defines no compressed/minimal solution encoding at all — it
emits raw index arrays only (`Proof::inputs: Vec<Input>`). This port
makes no wire-format claim; if a compact encoding is wanted downstream,
`Req/rust/src/lib.rs::get_minimal_from_indices` is a separate,
already-existing implementation for a *different* convention (Requihash/
Equihash's own `i mod k` leaf keying) and is not compared against here —
comparing would conflate two different index-generation schemes (RK's
plain sequential leaf index `i` vs. Requihash's `(i mod k, i div k)`
keying), not just two encodings of the same indices.

## Layout

- `src/lib.rs` — the ported algorithm. No I/O, no CLI. Entry points:
  `Equihash::new`, `.find_proof()`, `Proof::test()`.
- `src/bin/rk_gen.rs` — CLI driver: `rk_gen <n> <k> <seed>` prints one
  `{"n":...,"k":...,"seed":...,"nonce":...,"indices":[...],"verified":...}`
  JSON line, matching the (unchecked-in) C++ vector generator's output
  shape exactly — usable to extend `vectors/` at new parameter points
  without needing the C++ original again.
- `tests/cross_check.rs` — validates the Rust port against every file in
  `vectors/`.
- `vectors/` — the 8 committed KAT vectors (see "Validation").
- `src/bin/rk_bench.rs` — `reqbench`-discipline benchmark harness (7-rep
  min/median/MAD, git-provenance-stamped, counting-allocator peak memory
  cross-checked against OS RSS). `cargo run --release --bin rk_bench --
  --json baselines/<tag>.jsonl --tag <tag>`.
- `baselines/apple-silicon.jsonl` — one recorded baseline run (see below).

## Measured performance (Rust port, release build)

From `baselines/apple-silicon.jsonl`, 7 reps each, this machine (arm64,
48GB RAM):

| Case | wall (median) | peak mem (allocator / OS RSS) |
|---|---|---|
| (60,4) | 3.2ms | 2.14 MB / 4.28 MB |
| (90,5) | 33.1ms | 18.38 MB / 28.98 MB |
| (100,4) | 1798.3ms | 548.00 MB / 657.92 MB |
| (108,5) | 910.7ms | 149.00 MB / 581.55 MB |

**Allocator-vs-RSS cross-check disagrees (16.7%-74.4%) at every point,
flagged rather than silently averaged away per `Req/BENCH.md` §4.** This
is expected, not a bug: `find_proof`'s nonce-retry loop calls
`initialize_memory` fresh on every attempt (each allocating and dropping
a full `tuple_list`), and the counting allocator correctly reports the
peak of a *single* attempt, while OS RSS is a point-in-time read taken
right after the whole multi-attempt call returns — it reflects the
process's allocator-arena high-water mark across *all* attempts in the
run (macOS's allocator does not eagerly return freed pages to the OS), so
RSS never drops back down even though live Rust-heap usage does between
attempts. The two instruments are answering different questions here
(single-attempt peak vs. whole-process high-water mark), not disagreeing
about the same one — see "Suggestions for further investigation" for how
to make them directly comparable if that's wanted later.

## Suggestions for further investigation / follow-on tests

- **`(192,7)`/`(200,9)` timing+memory**, with an explicit time/memory
  budget agreed in advance (the k=4 `(120,4)` measurement above — 162s,
  10.5GB — is the closest available proxy by `tuple_n` order of
  magnitude, but k=7's larger per-tuple footprint and k=9's `O(k)`
  resolve cost make this a real unknown, not a safe extrapolation).
- **Reconcile the allocator/RSS memory disagreement** by isolating a
  *single* `initialize_memory`+`fill_memory`+`resolve_collisions×k` pass
  at a fixed, known-good nonce (skip the retry search) — that would let
  the counting allocator's peak and a freshly-read RSS answer the exact
  same question, rather than single-attempt-peak vs. whole-run-high-water-mark.
- **Compare RK's naive resolve-collisions cost against `Req/`'s own
  `reference`/`arena`/`bucket` solvers** at matching `(n,k)` points — RK
  predates every 2016-17 optimization technique `Equihash.md` documents
  (no index pointers, no incomplete bucket sort, no arena reuse — see
  `ResolveCollisions`'s `Vec<Vec<Tuple>>` full reallocation every round),
  so it should be the slowest/most memory-hungry of every solver in this
  corpus at the same parameters; confirming that directly would be a
  useful sanity check on the corpus's other measured numbers.
- **`rk_gen` at additional parameter points** beyond the 8 committed
  vectors — the binary exists specifically so this doesn't require
  rebuilding the C++ original again ([original/](original/)'s `rk_vecgen` provides the
  independent C++ side at any new point — build it with CMake and diff
  the two generators' JSON lines directly).
