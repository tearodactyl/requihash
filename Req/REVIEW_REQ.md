# REVIEW_REQ.md — review of the Req solver/verifier implementations (T2.3)

First executed 2026-07-17. Scope per `PLAN.md` §3 T2.3: `rust/src/solve/`,
`rust/src/verify/`, plus the `lib.rs` solver/verifier bodies they delegate to,
and (for cross-implementation comparison) `cpp/solver.h`/`requihash.h`.
Findings are numbered F1–F14; each carries its resolution status. The
corner-case inventory (same-day follow-up passes) and validation evidence are
at the end.

Evidence grades follow `BENCHMARK.md` convention. All test runs below were on
a Linux/aarch64 sandbox VM — correctness-grade only, **not** baseline-grade;
`BENCH.md`-disciplined re-benching of the touched hot paths on the reference
M4 machine is the one open action (see "Pending" at the end).

## F10 (found during F5/F6 execution) — vacuous root check: latent correctness bug — FIXED

The one real bug the review surfaced, present in three places:

- `rust/src/verify/arena.rs:82` (pre-fix)
- `rust/src/verify/early.rs:71` (pre-fix)
- `cpp/solver.h` `IsValidSolutionEarly` root loop (pre-fix line 315)

**Mechanism.** A row's expanded hash is `(k+1)` segments of `cbyte` bytes.
Round r requires the pair's segment at offset `(r-1)·cbyte` to be equal, and
the merge XORs pairs full-width — so the collided segment becomes zero in the
merged row and stays zero thereafter. After k rounds, bytes `[0, k·cbyte)` are
zero **for any input that passed the collision checks**, solution or not. The
root check in all three places inspected exactly those bytes:

```rust
// verify/arena.rs (pre-fix) — the checked range is zero by construction
if hashes[..k * cbyte].iter().any(|&c| c != 0) {
    return Err(Error::NonZeroRoot);
}
```

```cpp
// cpp solver.h IsValidSolutionEarly (pre-fix) — same bound, same vacuity
for (size_t t = 0; t < P.k * cByte; t++)
    if (level[0][t]) return false;
```

The real solution condition — XOR of all `2^k` hashes zero over the full n
bits — lives in the final segment `[k·cbyte, (k+1)·cbyte)`, which the k rounds
never check and these root checks never read. Consequence: `verify-arena`,
`verify-early`, and C++ `IsValidSolutionEarly` accepted **near-misses** (k
valid collisions, canonical ordering, nonzero final segment). The reference
verifiers (Rust `is_valid_solution`, C++ `IsValidSolution`) check the whole
row and were always correct.

**Why tests missed it.** `all_verifiers_agree`'s only negative case was a
swapped-leaf tamper, which fails at the ordering check; no test ever presented
an input that survives to the root. Lesson: negative tests must cover **every
rejection path** of every backend, not one representative tamper.

**Fix.** Both Rust verifiers rewritten to the shrinking-stride layout (drop
the collided `cbyte` at each XOR, comparisons at offset 0 — the same layout
the solver backends use), which makes the surviving bytes after k rounds
*exactly* the final segment; the root check now reads precisely the right
bytes by construction. C++ Early fix is minimal: bound corrected to `full`.

**Falsification.** New regression test
`all_verifiers_reject_nonzero_root_near_miss` (lib.rs) harvests real
near-misses from a Wagner merge at (48,5) and requires every verifier to
reject them, reference specifically with `NonZeroRoot`. Run against the
pre-fix verifiers (restored from git into a scratch copy): FAILS —
`verify-arena accepted a nonzero-root near-miss`. Against the fixed tree: 20/20
lib tests pass (25/25 with `--features rayon,simd,blake3`); C++ `req_test`
ALL PASS. Measured (VM), reproducible.

**Residual gap — closed same day.** C++ test parity landed in the corner-case
pass: `req_test.cpp` now harvests near-misses and runs the full rejection
matrix against both C++ verifiers (see "Corner-case inventory" below).

**Upstream comparison.** Zcash is not affected: the pinned `equihash` crate
(`third_party/equihash-0.3.0-vendored/src/verify.rs` — what zebra/librustzcash
use, and what A25's 46 KATs validate against) trims the collided `cbyte` at
every merge (`from_children`, `.skip(trim)`) and roots-checks the surviving
remainder — the same shrinking design this fix adopts. The bug was introduced
by this repo's own full-width/moving-offset measurement variants, i.e. by the
deviation from upstream's layout; the repo's reference verifiers (which follow
the zcash/zebra conventions) were always correct.

## F1 — regularity binding triplicated vs. README's "one place" claim — FIXED (both options, owner directive)

Three sites keyed a leaf by `(leaf mod k, leaf / k)`: `lib.rs leaf_row`
(canonical), `lib.rs solve_arena`'s serial fill closure, `bucket.rs` leaf
generation. Verified byte-identical semantics at all three (same LE encoding,
same `expand_array` call, same output slicing); the inline copies existed to
reuse one digest scratch buffer across leaves.

Resolution (owner: "both"): (a) a zero-alloc `leaf_row_into(leaf, digest,
out)` helper now carries the binding — the *only* site — with `leaf_row`
delegating to it and the two hot fill loops (`solve_arena`, `bucket.rs`)
calling it with a shared scratch, preserving the zero-per-leaf-allocation
property (bucket.rs even drops its former per-leaf `expand_array` Vec);
(b) README's "Key implementation note" amended to name the helper and the
call structure. Hot-path change → included in the pending M4 re-bench.
`base_clone` became dead in the process and was removed (zero-warnings
discipline, cf. A26).

## F2 — duplicate distinctness helpers — FIXED

`lib.rs` carried two byte-identical private helpers, `distinct` and
`slices_distinct` (`EhIndex` = `u32`). Consolidated to one `pub(crate)
slices_distinct`; `bucket.rs`'s third variant (`.iter().any(|x| ....contains(x))`)
now calls it too. Sorted-merge alternative considered and rejected: per-pair
vectors in the merge are short; O(|a|·|b|) wins at these sizes.

## F3 — avoidable clones/allocations — FIXED (behavior-preserving subset)

- `solve_reference` / `solve_instrumented` / `bucket.rs` merge: canonical-order
  merged index vector now built once with `Vec::with_capacity` (was
  clone-then-extend); XOR now writes only the surviving suffix (was full-width
  XOR then `drain(..cbyte)` — an O(row) memmove per merge, in `solve_reference`).
- `bucket.rs` solution emit: `std::mem::take` instead of a second clone.
- `verify/arena.rs` merge: `mem::take` + `append` instead of clone-then-extend
  (ordering is checked before the move).
- `is_valid_solution`: capacity-built index merge.

Evaluated, **no change** (recorded so the next reviewer doesn't re-litigate):
`bucket.rs` `cursor = counts.clone()` (needed: prefix sums serve as both bucket
bounds and scatter cursors); `solve_arena_with_leaves` solution emit (two
allocations are inherent: owned solution + dedup scratch); `pointer.rs:229`
(one clone, already minimal); `early.rs` `indices.to_vec()` (needed for the
global distinctness sort).

## F4 — dead branch in `solve_arena_with_leaves` row-count update — FIXED

Pre-fix code (captured per directive; the pattern to avoid — two conditionals
on the same condition, the first's assignment dead):

```rust
nrows = if hstride == 0 { 0 } else { hashes.len() / hstride.max(1) };
if new_hstride == 0 {
    nrows = idxs.len() / new_icount;
}
```

`hstride` had just been assigned `new_hstride`, so the branches test the same
condition; when it held, the first assignment (`0`) was immediately
overwritten. The `.max(1)` guard was double protection inside the `else` where
`hstride != 0` is already known. Now a single `if/else`.

## F5 — arena verifier stride never shrinks — FIXED (implemented + tested)

Cross-implementation comparison first (per directive): the solver backends
(`solve_reference`, `solve_arena`, `bucket`, C++ `SolveArena`) all shrink rows
by `cbyte` per round; the pre-fix Rust arena/early verifiers and both C++
verifiers keep full width with a moving offset. Both formulations are
algebraically equivalent on the checked segments — the solvers are the
existence proof that shrinking is sound. Implemented the shrinking layout in
both Rust verifiers; as a structural bonus it eliminates the F10 bug class
(the root check can no longer address the wrong bytes — only the final
segment survives).

Operational equivalence checks (how we know the rewrite is right):
`all_verifiers_agree` (accept all real solutions, reject swapped-leaf tamper,
30 nonces × 2 param sets), the new near-miss test (reject at the root path),
`arena_matches_reference`, the 46 Zcash KATs (A25 harness), and the C++
cross-check vectors — all green post-rewrite. The C++ verifiers keep their
moving-offset layout (only the Early root-check bound was fixed): rewriting
them to shrink is cosmetic there and not worth the churn — recorded as
explicitly considered and declined.

## F6 — misused `Error::WrongLength` on unreachable fold invariant — FIXED

Three sites (`is_valid_solution`, `verify/arena.rs`, `verify/early.rs`)
returned `WrongLength` if the fold didn't end at exactly one row. Reachability
verified: the input-length gate forces `2^k` rows, each round pairs rows
exactly (`2^j` is even for j ≥ 1), so the invariant cannot fail on any input —
only via an internal bug. Misreporting an internal bug as an input rejection
is the wrong failure mode for a consensus-adjacent path; all three now
`assert_eq!` with an "internal:" message (fail loud, attributable). C++'s
equivalent (`if (X.size() != 1) return false;`) left as-is: `bool` returns
can't distinguish, and a C++ assert policy is an owner call.

## F7 — early verifier doc/implementation mismatch — FIXED (both)

Doc claimed "a single hash column per level"; implementation allocated one
`Vec<u8>` per node per round plus per-leaf Vecs. Both fixed: `early.rs` now
uses one flat buffer per level with the shrinking stride (aligned with
`verify/arena` and the solvers), and the doc states exactly that. The "never
materialises index vectors" property is preserved (span-slice comparisons).
Verifier cost is ~7 µs (`BENCHMARK.md`) — this was alignment and honesty, not
performance work.

## F8 — pointer.rs outside the `Solver` trait — NO CHANGE (analysis in PLAN-cited report)

The trait shape is right; `PointerSolverPrototype`'s inherent `solve` already
matches the trait signature, so promotion under T2.4 is a mechanical `impl`.
Registration doctrine (self-test/equivalence gate before adoption — cf. the
SIMD hasher gate, the rayon feature gate) argues for keeping it unregistered
until counting-sort + KAT validation land. Full option analysis in the T2.3
session report; decision: status quo until T2.4.

## F9 — `Requihash::solve()` delegate — NOT LEGACY, KEPT

Live call sites: `req_profile`, `req_bench`, four lib tests. Zero-cost
delegate to `solve_reference`. Revisit when T4.1 touches `req_bench` anyway.

## F11 — out-of-range index accepted on the API path — FIXED

Found while enumerating rejection paths: no verifier enforced
`index < 2^(ell+1)`. `leaf_row` hashes any `u32`, so an out-of-range index
flowed into collision checks instead of being rejected structurally. The wire
path cannot express such values (minimal encoding is `ell+1` bits per index),
so this was API-path-only — but the verifiers take raw `u32` slices. The
pinned `equihash` crate has no explicit check either (it relies on wire
decode); this repo's verifiers are used directly on index vectors, so an
explicit check is the right divergence. Fixed in all five verifiers (Rust
reference/arena/early via new `Error::IndexOutOfRange`, C++ both, returning
false), checked after distinctness, before any hashing.

## F12 — `Params` accepted n > 512, panicking later — FIXED

`hash_output() = floor(512/n) · (n/8)`, so n > 512 gives an output shorter
than one row and `leaf_row`'s `out[..n/8]` slice panics (C++: reads past the
digest). (520,4) passes all three divisibility gates. Both constructors now
reject n > 512 with an explanatory message; the n = 512 boundary has valid
instances (e.g. (512,31), cbl 16) and is accepted.

## F13 — k = 0 accepted, dividing by zero in the regularity binding — FIXED

The binding keys leaf `i` by `(i mod k, i / k)`; k = 0 is a panic (Rust) / UB
(C++), not a degenerate-but-defined instance — the earlier inventory entry
claiming "defined but degenerate" was wrong and is corrected below. Both
constructors now require k >= 1. k = 1 IS well-defined and stays valid: one
round, `leaf mod 1 = 0` (single list, regularity trivially satisfied),
solution = one colliding pair over the full n bits; valid instances exist
((48,1), cbl 24; cbl bounds force n ∈ {16..50} ∩ 8ℤ ∩ 2ℤ).

## F14 — collision bit length unbounded; sub-8 silently corrupts, over-25 overflows — FIXED

`expand_array_into`/`compress_array` accumulate through a `u32`: extraction
requires `bit_len + 7 <= 32`, i.e. **cbl <= 25**, and the one-extraction-per-
input-byte loop requires **cbl >= 8** — exactly the bounds zcash's own
`ExpandArray` asserts. Neither was enforced here:

- **cbl < 8 silently under-fills rows.** At (24,5) (cbl 4): 3 input bytes
  yield only 3 extractions against a 6-byte row — the tail of every expanded
  row stays zero. Both implementations do it identically, so it
  self-consistently "solved and verified" while not being real 4-bit Wagner.
  Consequence: `SIZING.md` §2a's (24,5) row (the 52× ratio endpoint) measured
  a structurally degenerate instance — noted there.
- **cbl > 25 breaks the accumulator** (shifts past 32 bits: debug panic /
  wrong bytes in release). This is the *binding* parameter bound, tighter
  than any flat cap on n and k-dependent: n <= 25·(k+1) — 150 for k=5, 200
  for k=7, 250 for k=9. It also caps SIZING's arithmetic sweeps: k=5 ends at
  (144,5), k=7 at (200,7); the k=9 row set (<=240) is unaffected. (It further
  guarantees leaf count 2^(cbl+1) <= 2^26 fits `EhIndex = u32`.)

Both constructors now enforce cbl ∈ [8, 25]; boundary instances tested
((48,5) low end cbl 8, (200,7) cbl 25).

## Corner-case inventory (follow-up pass, same day)

Every identified rejection/corner case, with coverage status. "Matrix" = the
Rust `rejection_path_matrix` test (exact `Error`-variant agreement across all
three Rust verifiers) and its C++ mirror in `req_test.cpp` (rejection-only —
C++ verifiers return `bool`).

| Case | Construction | Covered by |
|---|---|---|
| Wrong length | empty / truncated / extended | Matrix (Rust+C++) |
| Duplicate index | `t[1] = t[0]` | Matrix (Rust+C++) |
| Out-of-range index (F11) | `2^(ell+1)` and `u32::MAX` | Matrix (Rust+C++) |
| Ordering failure at **every** round r | swap the first two level-(r-1) subtrees — blocks stay pair-aligned so rounds < r pass; collision at r holds (XOR symmetric); ordering breaks exactly at r | Matrix (Rust+C++) |
| Collision failure at **every** round r | concat 2^(k-r+1) mutually leaf-disjoint harvested round-(r-1) rows, first two differing in the leading segment — internally valid to r-1, first pair fails collision at r | Matrix (Rust+C++) |
| Nonzero root / near-miss (F10) | harvest final-round rows with nonzero remaining hash | dedicated tests (Rust + C++) |
| Regularity (single-list keying) | swapped-leaf vector | pre-existing tests |
| Bad params: divisibility, k>=n, n>512 (F12), k=0 (F13), cbl ∉ [8,25] (F14) | constructor probes + boundary acceptances (k=1, cbl 8/25, n=512) | `params_rejected` (Rust), req_test (C++) |
| Cross-verifier variant agreement | all of the above assert the **same** `Error` variant from reference/arena/early | Matrix (Rust) |
| k = 0 | **rejected** (F13 — earlier "defined-but-degenerate" claim was wrong: `leaf mod 0` divides by zero) | constructor + tests |
| k = 1 | valid, single-round instance; accepted and covered by `params_rejected`'s acceptance list | tests (construction only) |
| Encoding-level (non-minimal encodings, trailing bits on decode) | separate seam (`req_xcheck`/vectors), not verifier logic | **unaudited — follow-up** |

Test-construction notes worth keeping: mutation of a valid solution can only
reach early checks (that is why F10 hid); round-targeted failures need
*harvested* structures — subtree swaps for ordering (cheap), disjoint-row
concatenation for collisions, near-miss harvest for the root. These
constructions are parameterized over r and reusable for any (n,k).

## Pending

1. **Host re-bench** of touched hot paths (`bucket.rs` merge + leaf fill,
   `solve_reference`, `solve_arena` leaf fill via `leaf_row_into`) per
   `BENCH.md` — VM numbers are not comparable to `baselines/`. Standard run:
   `rust/bench.sh`. Expected neutral-to-slightly-positive; ungraded until
   measured on the M4.
2. SPEC owner call: `SPEC.md` should state explicitly the index-range
   validity condition (F11) and the parameter bounds k >= 1, n <= 512,
   cbl ∈ [8, 25] (F12–F14) — currently implementation-enforced
   clarifications of spec intent; the frozen spec is not edited unilaterally
   here.
3. Encoding-seam audit (last inventory row) — candidate small T-item.
4. **Cross-implementation rejection vectors**: extend the `vectors/` format
   with negative vectors (one per rejection path, constructed as in the
   matrix tests) so `req_xcheck` can assert rejection parity across
   implementations, not just acceptance parity. Format precedent: the pinned
   `equihash` crate's `INVALID_TEST_VECTORS` (params, input, nonce, solution,
   expected error — 9 cases, exact-variant asserted). Note its coverage is
   mutation-class only (Collision/OutOfOrder/DuplicateIdxs) — **no
   nonzero-root case exists in any corpus surveyed** (zcash crate, this
   repo's KATs, Sequihash reference vectors), because mutations can't reach
   the root check and upstream's shrinking layout never made them need one.
   The harvested near-miss vectors are therefore the novel, high-value part;
   mutation cases are regenerable by any consumer and optional.
5. Consider a `SECURITY_ANALYSIS.md` lesson entry for F10's class
   ("wrong-bytes checks that pass vacuously survive equivalence testing when
   no test reaches the guarded path") — owner call.

## Why F10 was found by code inspection rather than by vectors/testing

The vector corpus and the equivalence tests only ever present (a) valid
solutions — solver outputs and KATs — and (b) mutations of valid solutions.
Near-misses occupy an input region neither generator reaches: a mutation
almost always breaks a round-1..k collision or the ordering check (so the
root check never runs), and a random index vector passes all k collision
rounds with probability ~2^-(k·cbl) — black-box fuzzing effectively never
gets there. Reaching the root check requires *structure-aware construction*
(run the merge, harvest nonzero-root finals), which nobody had written
because nothing pointed at that branch. Inspection pointed at it (the bound
`k*cbyte` read as suspicious against the (k+1)-segment layout); the harvest
test then falsified the old code in one run. Both are now in place — the
lesson is that every guarded branch needs a test constructed to reach it,
which is what the rejection-path matrix and pending item 4 institutionalize.
