# CS — the Sequihash k-list reference port, and six comparative variants

Canonical C++ port of the 2025/1351 paper's own "Sequihash" k-list Wagner
solver, plus six standalone re-implementations (`variants/v1`–`v6`)
exploring different merge/storage strategies against the same reference
oracle. All seven are correctness-tested against the same 4 vectors,
generated from the actual Python reference, not a re-derivation.

## Conventions — read before touching any code here

1. **`k` is a list count, not a tree-depth exponent.** The Python
   reference's `k` (asserted a power of 2) is the paper's own `K`,
   matching its `(n, K=2^k)` table — **not** `Req/`'s `(n,k)` convention,
   where `k` is tree depth and `2^k` is the solution size. Every file in
   this directory uses `k` = solution size, matching the Python
   reference exactly.
2. **Leaf encoding is an ASCII decimal string.** `nonce + f"{i}-{j}"`
   (16-byte nonce, then decimal `i`, `-`, decimal `j`, no leading zeros,
   no fixed width) — not `Req/`'s binary encoding. Reproduced exactly by
   every variant here, not translated.

## What's ported, what's not

Ported: `k_list_wagner_algorithm.__init__`/`compute_item`/`hash_merge`/
`compute_hash_list_on_the_fly` (no-trade-off path only)/`_solve`/
`solve`/`verify_results`. Not ported: `run_with_memory_trace` and all
`rich.Console`/`Panel` usage (profiling/presentation scaffolding, not
algorithm) — no variant here has a stdio/logging dependency in its
algorithm code; each has a separate driver for I/O.

**Unported feature, in every variant**: the paper's index-trimming
trade-off mode (`solve(index_bit_length=N)`) — composes on top of the
existing `compute_hash_list`/`hash_merge` primitives if wanted later,
not a redesign.

## The seven implementations

| | Storage/merge strategy | Built on | Purpose |
|---|---|---|---|
| **base** (`src/`) | Arbitrary-width big-endian bytes, `std::unordered_map` hash-join | — (direct Python port) | The reference oracle every variant validates against |
| **V1** (`variants/v1-fixedint/`) | Native `uint64_t`-limb fixed-width ints, same hash-join as base | base | Removes arbitrary-precision tax; the baseline every other variant builds on |
| **V2** (`variants/v2-bucket/`) | Counting-sort bucket partition (tromp/xenoncat technique #2) instead of hash-join | V1 | Tests whether bucket-sort beats `unordered_map` here the way it does for Requihash's own solver |
| **V3** (`variants/v3-pointer/`) | Parent-pair pointers into the previous round instead of growing index vectors | V1 | Measures the memory cost of index-pointer storage *under* Sequihash's k-list regularity — `SECURITY_ANALYSIS.md` §4.1/F-A4 predicts this is a liability, not a win, here; this variant makes that concrete |
| **V4** (`variants/v4-static-inplace/`) | Pre-sized arena with slot reuse across rounds (techniques #3/#4) | V1 | Tests allocator-overhead reduction without changing representation |
| **V5** (`variants/v5-unlimited-mem/`) | Class-prefix BLAKE2b precomputation (shared nonce+`i-` state cached per list) + full radix-sort merge | V1 | Assumes memory is free; tests how much a class-prefix hash-state cache buys |
| **V6** (`variants/v6-square-khovratovich/`) | Structural mirror of Khovratovich's original C++ (`InitializeMemory`/`FillMemory`/`ResolveCollisions`/`FindProof`, 2D bucket table) | V1 | A second, independently-shaped differential oracle — a bug surviving two structurally unrelated ports is more likely a spec ambiguity than an implementation slip |

Every variant keeps V1's fixed-width-integer representation, so the only
thing that varies across V2–V6 is merge/storage strategy, not the
integer-arithmetic baseline.

## Known issues

None open. Two fixes below explain the current memory/timing numbers:

- **V4's index pool** reserves index slots per round depth, not a flat
  `K` slots for every row (a leaf needs 1 slot, not `K`) — peak at
  `(160,512)` is 1.79 GB.
- **V6's `RoundTable`** is one flat heap buffer per round, not one
  buffer per bucket (`nbuckets` allocations) — `(80,8)`-equivalent time
  is 3.81s.

## Current benchmark numbers

**CS-convention points** (n, K — K is the literal list count, NOT
`2^k`; see "Conventions" above), 5-rep min/median/MAD, Apple M4 Pro,
Release build, nonce `00112233445566778899aabbccddeeff` unless noted:

At `(n=80, K=8)`:

| Variant | min | median | MAD |
|---|---|---|---|
| V1 fixed-int | 4.36s | 4.40s | 0.013s |
| V2 bucket | 2.50s | 2.51s | 0.007s |
| V3 pointer | 2.50s | 2.53s | 0.013s |
| V4 static+inplace | 2.87s | 2.90s | 0.015s |
| V5 unlimited-mem | 3.29s | 3.30s | 0.014s |
| V6 square-Khovratovich | 3.81s | 3.84s | 0.038s |

V2/V3 are fastest; V1 (the unoptimized baseline) is slowest; V6 (a
structural, not performance, exercise) is second-slowest by design, no
longer a >2x outlier after its fix.

**Peak memory at `(n=160, K=512)`** (the largest committed vector,
2 solutions, all 7 implementations agree byte-for-byte on the solution
set):

| Variant | Peak RSS |
|---|---|
| V1 / V2 / V5 | ~350 MB |
| V6 | 2.64 GB |
| V3 | 3.25 GB |
| V4 | 1.79 GB |

V3's elevated memory is the deliberate finding of that variant (index-pointer
storage costs more, not less, once the k-list regularity constraint is
in play) — not a bug. V6's is the cost of its own 2D-bucket-table
approach (`kSlotSlack=64` slots/bucket, ~4 real occupants on average) —
present by structural choice, already reduced once by the flat-storage
fix above.

## Validation

All 7 implementations pass the same 4 vectors (`vectors/*.json`,
generated by running the actual Python reference, not a
reimplementation): `(24,8)`, `(40,16)`, `(64,128)`, `(160,512)`. Each
variant has its own `tests/differential.cpp`/`cs_v*_differential`
(wired into `ctest`) asserting exact index-vector equality plus
self-verification.

```
cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
```

Run the same from each `variants/vN-*/` directory for that variant.

**k=1 note**: the Python reference's own `_solve` never runs its
collision-check loop when `k=1` (the `for i in range(1,k)` loop body
never executes), so it returns every leaf as an unverified "solution."
V1–V5 (and base) reproduce this byte-exact-to-the-reference quirk
faithfully; V6 deliberately diverges and returns zero solutions instead
(the arguably-more-correct behavior). Not a bug in either — a documented
difference in how faithfully each variant tracks the reference's own
degenerate-input handling. Not a useful test point for anything else:
`k=1` exercises no merge tree, no regularity binding.

## Benchmark drivers

Each implementation has a `cs_bench`/`cs_v*_bench` binary:
`<n> <K> <nonce_hex> [--reps N] [--no-save]`, 5-rep min/median/MAD wall
time + peak RSS, one JSON line matching
`reqbench::run_record::RunRecord`'s schema. Every invocation writes a
new file to that implementation's own `runs/<n><K>_<timestamp>.jsonl` —
never appends, never touches another implementation's directory. A bad
`(n,K)` is rejected immediately (before any solve work starts) with a
message naming which rule failed — see `src/nk_check.hpp`.

## Build environment

Vendored BLAKE2b (`BLAKE/vendor/blake2/blake2b-ref.c`, Samuel Neves,
CC0), referenced repo-relative; override with `-DBLAKE2_REF_DIR=...`.
`project(...)` must declare both `CXX` and `C` — `CXX`-only silently
drops `blake2b-ref.c` from the build with no configure-time error, only
a late undefined-symbol linker error.

## Layout

- `src/klist.hpp`/`.cpp` — base port. `src/cs_gen.cpp` — correctness
  driver. `src/cs_bench.cpp` — benchmark driver. `src/nk_check.hpp` —
  shared (n,K) pre-flight validity check, copied verbatim into each
  variant's own `src/`.
- `variants/vN-*/src/klist_vN.{hpp,cpp}` — each variant's algorithm.
  `fixedint.hpp` (V1–V6) and `hashmsg.hpp` (V1–V5) are byte-identical
  copies across variants (namespace-renamed only) — shared logic,
  incidentally duplicated per this corpus's standalone-port convention,
  not independently maintained.
- `vectors/` — the 4 committed KAT vectors, shared by every
  implementation's differential test.

## Open questions / suggested follow-on work

Tracked in `Req/PLAN.md` T5.2, not duplicated here.
