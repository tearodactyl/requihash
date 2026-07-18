# ARCHITECTURE.md — Mix-and-Match Backend Structure for Req

This proposes a code structure that lets the hash primitive and the solver
backend be swapped freely across three implementation tiers — a linear reference,
a parallelized native implementation, and special-instruction / hardware-
accelerated paths — without the consensus-critical logic (encoding, tree
validation, difficulty) knowing or caring which is in use.

The design is not invented from scratch; it is the union of two patterns already
proven in the reference codebases this project borrows from:

- zcash `src/crypto/sha256.cpp`: **function-pointer dispatch selected once at
  startup** by `SHA256AutoDetect()`, with named backends (`sha256_sse4`,
  `sha256_avx2`, `sha256_shani`) each exporting `Transform` and batched
  `Transform_Nway` variants, every candidate **self-tested before adoption**.
- zebra / `blake2b_simd`: **runtime CPU feature detection inside a fat binary**
  that ships scalar + SSE4.1 + AVX2 and dispatches to the fastest, plus a
  **`hash_many` batched API** that is the real throughput lever for mining.

## 1. The two seams

Only two things vary across tiers; everything else is fixed. Name the seams
precisely and the rest of the system is backend-agnostic.

**Seam A — the hash primitive.** Requihash needs exactly two operations from its
hash, and they are different enough to separate:

- `hash_one(person, prefix, leaf) -> digest` — a single leaf. Latency-bound.
  Used by the *verifier* (2^k leaves) and by small-parameter tests.
- `hash_many(person, prefix, leaves[]) -> digests[]` — a batch of independent
  leaves. Throughput-bound. Used by the *miner* (2^(ell+1) leaves). This is where
  SIMD lanes, GPU warps, and dataflow tiles actually pay off, because Requihash's
  regularity keying (`leaf -> (leaf mod k, leaf / k)`) makes every leaf
  independent — embarrassingly parallel by construction.

**Seam B — the solver backend.** Wagner's tree search over the generated leaves:

- `solve(engine, base) -> Vec<solution>` — reference (scalar, one-list-at-a-time),
  native-parallel (bucket-partitioned, multi-threaded), or offloaded (GPU/FPGA).

Note on emphasis: BENCHMARK.md measures that at these parameters the *merge*
(Seam B), dominated by per-row allocation, is 76-87% of solve time, while leaf
hashing (Seam A) is only 13-24% and falling with n. So although both are seams,
the profile says the first optimization effort belongs on Seam B (arena
allocation, radix bucketing) — batched-SIMD hashing on Seam A is the third win,
not the first. The seam structure is unchanged; the priority ordering is the
correction the data forced.

The verifier is deliberately *not* a seam. It stays single, scalar, portable, and
auditable, because it is the consensus-critical path ("verification
must stay boring" — the consensus-path design principle). A miner may be exotic; a verifier may not.

## 1a. Not married to any specific backend, binding, or GBP parametrization

Stated explicitly because it's easy to read the rest of this document — and
`SOLVER_CORPUS.md`'s RZ/RT ports, which target Zcash/Zebra's own tromp-derived
C and `blake2b_simd` specifically — as this project committing to those as
*the* implementation. It is not. Both seams (§1) exist so this project can
go to primary sources, evaluate real candidates on their own merits, and
swap freely — not so it can standardize on Zcash/Zebra's current choices or
on BLAKE2/BLAKE3 as a pair:

- **Seam A is open past BLAKE2 and BLAKE3 entirely.** Zcash's `blake2b_simd`
  binding (either generation — the raw-C-ABI shape the pinned `equihash`
  crate/Zebra use, or zcashd's newer `cxx::bridge` one) and BLAKE3 are two
  *candidates* this project has measured (BENCHMARK.md §9), not the
  boundary of the search. If a different hash — a different BLAKE2/3
  variant, a different SHA-3 family member, something else entirely —
  proves better on the actual selection criterion below, it goes in Seam A
  the same way blake2b/blake3 did, with no special status for either
  Zcash's specific bindings or the Blake family's brand name.
- **Seam B is open past tromp's Equihash solver, "neutered" or otherwise.**
  RT explicitly targets tromp's *own* full multi-core original (not the
  single-core-stripped Rust-wrapped copy Zebra actually runs, which is
  RZ's separate target) precisely because this project wants the real,
  best-available design as a candidate — not a Rust-convenience rewrap of
  whatever a downstream consumer happened to freeze. Both RK (Khovratovich's
  original) and RT (tromp's design) are being evaluated on their own terms
  as solver candidates, not ported out of loyalty to being "the Equihash
  lineage."
- **The GBP parametrization itself is not fixed to Equihash or Sequihash
  either.** `Req/SPEC.md`'s own opening frames its whole
  `PoW(n, k, hash, m, keying, context)` family as "a design freedom of this
  program... not asserted as canonical against the wider design space."
  `UNIHASH.md` already treats the regularity binding's concrete encoding
  (this project's `le32(class)||le32(counter)` vs. the paper's own
  `f"{i}-{j}"` ASCII form) as one arbitrary choice among several compatible
  ones — a precedent for treating `keying`, `encoding`, and eventually the
  generalized-birthday structure itself as points in a wider space, not
  fixed commitments to Equihash's or Sequihash's specific choices. A future
  parametrization that resembles neither is explicitly in scope, not a
  departure from this project's premise.

This liberty extends to the *conditions* themselves (2026-07-17): the
regularity constraint is one point in condition space — this project may
modify it or add others. The discipline is that objectives come first:
name what the condition is meant to improve (tradeoff steepness,
verification cost, wire size, ASIC memory economics, …), get the
measuring apparatus for that objective (T3.1's counting harness is the
first), and only then evaluate candidate conditions against it. A
condition adopted before its objective is measurable would repeat
Equihash's own history — a binding whose central security property went
unmeasured for a decade (`../Equihash.md`, the steepness story).
- **The actual selection criterion, stated plainly**: a quality-proven,
  stable implementation — real primary-source code with a track record,
  not a paper's asymptotic claim alone — that supports acceleration on
  *both* x86 and Apple-Silicon/ARM (the two platforms this project
  actually builds and measures on), evaluated by running it, not by
  reputation. C/C++ reference implementations wrapped by thin Rust
  iterators/FFI are an accepted shape at either seam when that's what the
  best-available source is written in (RZ's `blake2b_glue.c` trick and
  RT's planned FFI-or-subprocess cross-check binary are both examples of
  this pattern already in use) — the wrapper language is not itself a
  selection criterion; implementation quality and cross-platform
  acceleration support are.
- **Version policy: no required pin.** This project has not established any
  required pinned version for a dependency at either seam. Where a version
  looks pinned in practice (e.g. `equihash = "0.3"`), that reflects
  *upstream's* choice (Zebra's own `Cargo.toml`), not a constraint this
  project has adopted — and in that specific case `0.3.0` is also simply
  the only version of that crate published on crates.io, not a stale
  pin. Default to latest stable, or the most widely-adopted recent version
  where "stable" is ambiguous, for anything this project depends on
  directly (`Req/rust/Cargo.toml` already does this via loose `"1"`
  semver for `blake2b_simd`/`blake3`/`rayon`) — move off a version only
  when a known incompatibility is the actual reason, stated as such, not
  out of habit or unexamined deference to what an upstream consumer
  happens to run.

## 2. Trait / interface at each seam

Express each seam as a narrow interface the rest of the code programs against.

### Rust

```rust
/// Seam A: the hash primitive. Implementors provide one-shot and batched leaf
/// hashing; the batched default falls back to a loop over hash_one so a new
/// backend only has to implement the fast path when it has one.
pub trait LeafHasher: Send + Sync {
    /// Digest length this backend emits (must equal Params::hash_output()).
    fn output_len(&self) -> usize;

    /// One leaf: H(person || prefix || le32(class) || le32(counter)).
    fn hash_one(&self, person: &[u8; 16], prefix: &[u8], class: u32, counter: u32,
                out: &mut [u8]);

    /// A batch of leaves. `keys` are (class, counter) pairs; `out` is
    /// keys.len() * output_len() bytes. Default = serial hash_one.
    fn hash_many(&self, person: &[u8; 16], prefix: &[u8],
                 keys: &[(u32, u32)], out: &mut [u8]) {
        let n = self.output_len();
        for (i, &(c, j)) in keys.iter().enumerate() {
            self.hash_one(person, prefix, c, j, &mut out[i * n..(i + 1) * n]);
        }
    }

    /// Human-readable backend name, for logging the selected path.
    fn name(&self) -> &'static str;
}

/// Seam B: the solver backend. The verifier is intentionally not part of this.
pub trait Solver {
    fn solve(&self, engine: &Requihash, hasher: &dyn LeafHasher) -> Vec<Vec<EhIndex>>;
    fn name(&self) -> &'static str;
}
```

### C++

```cpp
// Seam A: match zcash's function-pointer style so a backend is a struct of fn
// pointers, selected at startup and self-tested.
struct LeafHasher {
    size_t output_len;
    const char* name;
    void (*hash_one)(const uint8_t person[16], const uint8_t* prefix, size_t plen,
                     uint32_t cls, uint32_t counter, uint8_t* out);
    // out = count * output_len bytes; keys = count*2 u32s (class,counter interleaved)
    void (*hash_many)(const uint8_t person[16], const uint8_t* prefix, size_t plen,
                      const uint32_t* keys, size_t count, uint8_t* out);
};

// Seam B
struct Solver {
    const char* name;
    std::vector<std::vector<eh_index>> (*solve)(const Requihash&, const LeafHasher&);
};
```

## 3. Selection: detect once, self-test, freeze

Copy zcash's discipline exactly — pick the backend once at startup, prove it
against the reference before trusting it, then never branch per-call.

```rust
/// Returns the fastest LeafHasher the platform supports, after checking it
/// agrees with the scalar reference on a fixed known-answer vector.
pub fn autodetect_hasher() -> Box<dyn LeafHasher> {
    let reference = Blake2bScalar::new();
    let candidates: Vec<Box<dyn LeafHasher>> = vec![
        #[cfg(target_arch = "x86_64")] maybe_avx2(),      // None if unsupported
        #[cfg(target_arch = "aarch64")] maybe_neon_batch(),
        Some(Box::new(Blake2bSimdMany::new())),           // crate-backed, portable
    ].into_iter().flatten().collect();

    for c in candidates {
        if agrees_with_reference(&reference, c.as_ref()) {   // <-- self-test gate
            log::info!("Req leaf hasher: {}", c.name());
            return c;
        }
        log::warn!("Req leaf hasher {} failed self-test, skipping", c.name());
    }
    Box::new(reference)
}
```

The self-test gate is not optional: a SIMD or GPU backend that is 1 bit off
silently forks consensus. zcash asserts `SelfTest()` inside `SHA256AutoDetect()`
for exactly this reason, and Requihash inherits the requirement because the leaf
hash feeds the validity check directly (Equihash.md F-A1: the whole scheme lives
or dies at the hash-to-leaf boundary).

## 4. Directory layout

```
Req/
  common/                     # tier-independent, shared by C++ and Rust semantics
    SPEC.md                   # the frozen wire format + regularity constraint
    vectors/                  # cross-language known-answer + solution vectors

  cpp/
    requihash.h               # params, encoding, GenerateHash contract (Seam A caller)
    verify.h                  # THE verifier — scalar, single, never a seam
    hash/
      blake2b_ref.h           # tier 1: linear reference (current blake2b.h)
      blake2b_sse.cpp         # tier 3: SSE4.1/AVX2 (borrow zcash sha256_* pattern
      blake2b_neon.cpp        #          or link libb2 / BLAKE3 team's blake2)
      dispatch.cpp            # autodetect + self-test, sets the LeafHasher struct
    solve/
      solve_ref.h             # tier 1: scalar Wagner (current solver.h)
      solve_native.cpp        # tier 2: bucket-partitioned, std::thread / OpenMP
      solve_cuda.cu           # tier 3: optional, offloads hash_many + bucketing
    CMakeLists.txt            # feature flags: REQ_ENABLE_AVX2, REQ_ENABLE_CUDA...

  rust/
    src/
      lib.rs                  # params, encoding, verifier (Seam-agnostic core)
      hash/
        mod.rs                # LeafHasher trait + autodetect
        scalar.rs             # tier 1: linear reference (current blake2b.rs)
        simd.rs               # tier 3: blake2b_simd many::hash_many wrapper
      solve/
        mod.rs                # Solver trait
        reference.rs          # tier 1
        rayon.rs              # tier 2: rayon bucket-parallel
    Cargo.toml                # features = ["simd", "rayon", "cuda"]
```

Two rules keep the seams honest. The verifier (`verify.h`, `lib.rs` verify path)
depends on **only** the reference hasher — never on `dispatch`/`autodetect` — so a
broken accelerated backend cannot reach consensus validation. And `SPEC.md` plus
`common/vectors/` are the contract every tier is tested against, so "does the AVX2
miner agree with the scalar verifier" is a mechanical check, not a hope.

## 5. Feature-flag matrix

Tiers compile in or out; the default build is reference-only and dependency-free.

| Cargo/CMake feature | Tier | Backend | Falls back to |
|---|---|---|---|
| (none) | 1 | scalar BLAKE2b, scalar Wagner | — |
| `simd` / `REQ_ENABLE_AVX2` | 3 | `blake2b_simd` hash_many / AVX2 | scalar, at runtime, via self-test |
| `rayon` / `REQ_OPENMP` | 2 | bucket-parallel solver | reference solver |
| `neon` (aarch64) | 3 | batched leaf hashing | scalar |
| `cuda` / `REQ_ENABLE_CUDA` | 3 | GPU hash_many + bucketing | native, if no device |

Runtime, not just compile-time, fallback matters: a binary built with `simd`
still runs on a machine without AVX2 because `autodetect_hasher()` drops any
candidate that fails detection or self-test. This is the `blake2b_simd`
fat-binary model.

## 6. Why this shape and not an enum or generic

Three alternatives were considered and rejected:

- **`#[cfg]` at every call site** — scatters the backend choice across the solver
  and makes the "verifier uses only the reference" invariant unenforceable. The
  seam collapses.
- **Generic `Solver<H: LeafHasher>` monomorphized** — fine for the miner, but the
  runtime-detected hasher is a `dyn` object by nature (the CPU decides at
  startup), so a trait object at Seam A is the honest type. The per-leaf virtual
  call is amortized by `hash_many` operating on whole batches, not one leaf.
- **Depend on one SIMD crate and stop** — good for Rust today, but forecloses the
  hardware tier (a convergent inference-hardware MAC substrate, GPU/FPGA offload) and the C++
  side. The trait keeps that door open at the cost of one indirection.

The structure costs one function-pointer indirection per *batch* (not per leaf)
and one startup detection. In exchange, the same core encoding + verifier +
difficulty logic is written once and every acceleration tier — present and future,
CPU SIMD through inference-hardware MAC arrays —
plugs into the two named seams behind a self-test gate.
```

## 7. Solve-seam backends: which 2016-17 technique each one implements

The 2016-17 Equihash optimization wave produced four representational
techniques (index-pointer storage, incomplete bucket sort, static allocation,
in-place merge) that took the reference solver from the paper's naive form to
the memory floor — their origin, authorship, and the 178MB/144MB/49MB record
are covered in full in [`../SOLVERS.md`](../SOLVERS.md) §0, not repeated here.
This section is the concrete record of which of Req's own Seam-B backends
implements which technique, and what it measured.

In Equihash, techniques 1-2 are what collapsed ASIC resistance (they fix the
memory-access pattern an ASIC wants). In **Requihash they are safe to use**,
because the regularity constraint blocks the single-list algorithm those
techniques accelerate — the k-list solver they run on costs an ASIC ≥2x memory
rather than less (`../Requihash.md` F-A4). So Req adopts the performance wins
without reopening the vulnerability.

| Technique | Prior state | Applied? |
|---|---|---|
| 2. Incomplete bucket sort | full `sort_by` (24% of solve) | **Yes** — `solve::bucket::BucketSolver` |
| 3. Static allocation | per-round `Vec` growth | **Partial** — arena preallocates the leaf buffer; bucket counting-sort arrays sized from param, folded into the arena/bucket wins |
| 4. In-place merge | fresh out-buffers each round | **No** (deliberate) — the arena's flat SoA already avoids per-row alloc; in-place would save the round's out-buffer alloc but complicates the bucket scatter — deferred |
| 1. Compact index-pointer storage | full explicit index vectors per row | **Prototype only** (`rust/src/solve/pointer.rs`, `Req/PLAN.md` A6) — proves the pointer-tree representation reconstructs correctly against `solve_reference` and the verifier; not wired into `all_solvers()`, not KAT-validated, not memory-measured. Staged 3-approach path to production (graft `bucket.rs`'s counting-sort merge → tromp's xhash early-reject → bucket-addressing only if profiling justifies it) and the language/style rationale (extend this Rust prototype, don't port tromp's C bit-packing) are in `pointer.rs`'s own module docs |

Measured timings for this progression (reference → arena → bucket, and the
bucket-vs-rayon-parallel comparison) are [`BENCHMARK.md`](BENCHMARK.md) §6's
data — not repeated here to avoid two copies of the same numbers drifting
apart. All three solvers produce byte-identical solution sets
(`all_solvers_agree` test), so every optimization is correctness-preserving.

## 7a. The technique arithmetic: indexes, buckets, pairs, and slack

§7 records *which* backend applies which 2016-17 technique; this section is
the quantitative companion — what each technique does to the index
representation, how the bucket geometry is calculated, what the pair
encodings cost in bits, and what completing techniques 3-4 here would buy.
Formulas tie into `SIZING.md` §1c's full-index peak model,
`N·(3·cbyte + 12·2^(k−1) + 96)` bytes. Constants cited from the RZ port
(`SOLVER_CORPUS/rz`, tromp's (144,5) miner) and `../SOLVERS.md` §0
(xenoncat) are code-verified.

### 7a.1 Indexes are the through-line

Notation used throughout §7a: **r** is the round number, r ∈ 1..k — a row
that survives round r is the XOR of `2^r` leaves (round 1 pairs two
leaves, round 2 pairs two round-1 rows = four leaves, and so on to the
root's `2^k`). "Digit" is the round's ell-bit collision segment; since
each round consumes exactly one, *digit and round are used
interchangeably* (tromp's code says digit).

Every technique is, at bottom, a statement about row *provenance* — how a
row remembers which leaves it came from:

1. **Index-pointer storage** replaces the accumulated index tuple
   (`4·2^r` bytes per round-r row: all `2^r` leaf indices at 4 bytes
   each — the source of the SIZING §1c model's dominant `12·2^(k−1)`
   term) with one fixed-size reference to the two prior-round rows that
   were XORed; full indices rematerialize only during solution
   reconstruction.

   **The "tree", concretely**: provenance under this scheme is a binary
   tree — each round-r row is an internal node whose two children are
   round-(r−1) rows, with leaves (round 0) the raw indices. *Node type*:
   in tromp, a packed 32-bit word `bucket ‖ slot0 ‖ slot1` naming the two
   children by their storage address (his struct is literally named
   `tree`); in this repo's prototype, `enum Ptr { Leaf(u32),
   Pair(u32, u32) }`. *Where used*: written once per surviving row during
   each merge round; read only at solution time, walking root → leaves to
   emit the `2^k` indices (tromp's `listindices`, our `reconstruct`).
   *Sizing*: one node per slot per round, and **every round's node array
   stays resident** until reconstruction — `σ·R·4` bytes per round
   (σ = slack, §7a.3), k rounds → `k·σ·R·4` total. At (200,9) with
   tromp's σ = 2: 9·2·2^21·4 ≈ 150 MB — i.e. the provenance tree IS the
   dominant share of the real 144–178 MB footprints, which is why its
   per-node width (§7a.4) was worth fighting over.

2. **Incomplete bucket sort** doesn't touch indices, but changes row
   *addressing* from a global row number (needs `ell+1` bits, up to 26)
   to `(bucket, slot)` — the precondition for pointer encodings that fit
   one machine word (§7a.4).
3. **Static allocation** makes `(bucket, slot)` addresses *stable* for the
   lifetime of the solve (fixed-capacity arrays are never reallocated), so
   tree nodes written in round r remain dereferenceable at reconstruction —
   and fixed widths are what make packed encodings decodable at all.
4. **In-place merge** recycles freed input slots, with the constraint that
   tree nodes must survive: tromp keeps every round's node arrays alive
   (`htalloc.trees0/trees1`) separately from the working pair lists, which
   is why in-place applies to the pairing workspace, not the provenance
   store.

### 7a.2 Incomplete sort and partition: definitions, mechanics, impact

**The requirement.** Each round must find every pair of rows whose current
ell-bit digit is *equal*. Equality-grouping is all that's needed — no
ordering between different digit values is ever consumed.

**What Wagner's textbook form does**: comparison-sort the whole list by
the digit (O(R log R)), then walk it once; equal digits end up adjacent.
This buys a total order and uses only the grouping.

**What the counting-sort partition does** (R = rows this round, B = 2^b
buckets keyed by the leading b bits of the digit):

1. *Count*: one pass over R rows, incrementing `counts[bucket(row)]`.
2. *Prefix-sum*: `counts` becomes each bucket's start offset — a partition
   of `0..R` into B contiguous ranges.
3. *Scatter*: second pass writes each row id into its bucket's next slot.

O(R + B) total, all passes sequential-friendly. **Why "incomplete"**: the
result is NOT sorted — buckets appear in digit-prefix order, but rows
*within* a bucket are in arrival order, and when b < ell (leaving
RESTBITS = ell − b unexamined bits) rows in one bucket may still differ in
the digit's tail. The within-bucket scan does the exact-equality grouping on those
remaining RESTBITS — cheap because expected occupancy μ = R/B is tiny.
The sort is left "incomplete" at exactly the point where equality-grouping
is satisfied and no further order would be used.

**Impact, separated:**

- *Time (measured here)*: the full sort was 24% of `solve_reference`;
  swapping it for the partition gave +14% over the arena backend, 1.86×
  cumulative over reference (`BENCHMARK.md` §6).
- *Representation (the historical payoff)*: after the scatter, a row's
  address is `(bucket, slot-within-bucket)` with slot < NSLOTS — a few
  bits instead of a 26-bit global row number. One-word tree nodes
  (§7a.4), and hence the provenance-tree sizing in §7a.1, exist only
  because of this addressing change. This mattered more than the sort
  time.

### 7a.3 Pair counts are data-dependent: the distribution and the slack heuristics

Definitions: **R** = number of rows in the current round's input list
(≈ N = `2^(ell+1)` in expectation, every round); **B** = number of buckets
= 2^b. Scattering R rows into B buckets gives per-bucket occupancy ~
Binomial(R, 1/B) ≈ Poisson with mean **μ = R/B**. The *total* surviving
rows per round stays ≈ R (candidate pairs on an exact ell-bit match total
`R²/2^(ell+1)` = R when R = `2^(ell+1)`), but *per-bucket* counts
fluctuate, so fixed per-bucket capacities need slack. Heuristics in use,
code-verified:

- **tromp / RZ** at (144,5): `BUCKBITS=20` (2^20 buckets), μ = 2^25/2^20
  = 32, `NSLOTS=64` — a **2× slack factor**, with overflow *counted and
  dropped* (`Slot` writes past `NSLOTS` increment the counter, rows lost).
  The tail math: P(Poisson(32) ≥ 64) ≤ e^(−μ(2ln2−1)) ≈ 4×10⁻⁶ per
  bucket; ×2^20 buckets ≈ **~5 overflowing buckets per round** (the
  counting sort runs once per round/digit), each dropping the row or two
  past its cap, out of 33M — a negligible solution-loss rate, and the
  reason "drop on overflow" is acceptable for mining (there is always
  another nonce). Same policy at (200,9) in his 2^12-bucket layout
  (μ = 512, NSLOTS 1024: 2× again).
- **xenoncat** at (200,9): 256 coarse buckets (chosen empirically over
  2048/1024/512), `PARTS=4` passes, plus *partial duplicate suppression*
  during middle stages so buckets don't fill with known-trivial rows — a
  different slack strategy: keep occupancy honest rather than cap harder.
- **This repo (`bucket.rs`)**: no caps, no drops — the counting pass sizes
  every bucket exactly, at the cost of dynamically grown out-buffers (the
  T2.5 target). For static out-buffers the remaining data-dependent
  quantity is the *output* row count. Rather than a slack heuristic, an
  exact pre-pass buys the number for one extra scan of the
  already-partitioned rows:

  ```rust
  // After count/prefix/scatter (order[], counts[] as in bucket.rs):
  // walk each bucket's exact-key groups WITHOUT emitting, and sum the
  // pairs each group will produce.
  let mut out_rows = 0usize;
  for b in 0..nbuckets {
      let (lo, hi) = (counts[b] as usize, counts[b + 1] as usize);
      let mut i = lo;
      while i < hi {
          let key = row_key(order[i]);                  // full cbyte digit
          let mut j = i + 1;
          while j < hi && row_key(order[j]) == key { j += 1; }
          let g = j - i;                                // group size
          out_rows += g * (g - 1) / 2;                  // C(g, 2) pairs
          i = j;
      }
  }
  // allocate ONCE, exactly: out_hashes = out_rows * new_stride bytes,
  // out_idxs with capacity out_rows; then the emit pass fills them.
  ```

  One honest caveat: the duplicate-leaf filter (`slices_distinct`) rejects
  a few pairs during the emit pass, so `out_rows` is an exact *upper*
  bound — the buffers never reallocate, with a sliver of unused tail.
  Recommended over guessing; fall back to `R·(1+δ), δ≈10%` + counted
  overflow only if the pre-pass measures as too expensive.

### 7a.4 Pair encodings: bit accounting, triangular (Cantor) vs bitfield

**The problem being encoded**: a tree node (§7a.1) must name *two*
prior-round rows. Naming a row by global index costs `ceil(log2 R)` =
ell+1 bits (25 at (144,5), 26 at ell=25), so a raw pair needs 50–52 bits —
no encoding tricks fit that in one 32-bit word. This repo's prototype
therefore spends two u32s (8 bytes). Bucket addressing (§7a.2) shrinks
each name to `(bucket, slot)` where the slot is tiny; then two encodings
compete for the one-word representation, differing in how they spend the
32-bit budget on `(bucket, slot0, slot1)`:

- **Bitfield (tromp)**: literal concatenation `bucket ‖ slot0 ‖ slot1`,
  b + 2s = 32. Encode = two shifts and two ORs; decode = shifts and
  masks. RZ code, (144,5): 20 + 6 + 6 = 32 exactly (`NSLOTS = 64`, so
  each slot field covers 0..63). His (200,9): 12 + 10 + 10. The two slot
  fields are *independent*, so the encoding can represent ordered pairs
  including (x, x) — some code points are wasted on pairs that never
  occur (a row isn't paired with itself, and pair order is irrelevant).
- **Triangular / Cantor rank (xenoncat)**: exploit exactly that waste.
  An unordered pair of distinct slots (s < t) is ranked
  `x = t(t−1)/2 + s` — the diagonal enumeration idea of Cantor's pairing
  function, specialized to 2-element subsets. Decode: `t = ⌊(1+√(8x+1))/2⌋`
  (inverse triangular root), `s = x − t(t−1)/2`. Value range for bucket
  capacity m: `C(m,2) = m(m−1)/2` — worked example at m = 64: bitfield
  needs 6+6 = 12 bits for its 4096 code points, triangular needs
  `⌈log2 2016⌉` = 11 bits for its 2016 *reachable* pairs. One bit per
  pair, always: `2·log2 m − 1` vs `2·log2 m`.
- **Why one bit matters**: at a fixed 32-bit word, the slot budget is
  what's left after bucket bits. One saved bit means **√2× more slots per
  bucket** at the same word width — equivalently, the same slots with one
  fewer bucket bit. That headroom is how tromp's adoption of Cantor
  coding funded his bucket-count reduction (2^12 → 2^10, 2016-11-17 — the
  commit `../SOLVERS.md` flags as the single most consequential
  optimization in the record; fewer buckets → smaller counts array →
  cache-resident scatter, §7a.5). Cost: the decode's integer square root
  vs the bitfield's mask — a real but small ALU price, paid only at
  reconstruction and comparison sites.

**"Halving 8", precisely**: going from this repo's 8-byte
`Ptr::Pair(u32, u32)` to a 4-byte one-word node is not a free encoding
change — it *requires* first adopting bucket-addressed, capacity-capped
storage (with §7a.3's slack-and-drop consequences), because only
`(bucket, slot)` coordinates are small enough to pack. That is
`pointer.rs`'s staged approach 3. **The profiling gate, quantified** (what "gated on
profiling" means — run the stage-1 pointer backend first, then):

- **(a) Peak-memory share.** Instrument: `req_memcheck` extended to tag
  allocations by structure (node arrays vs. hash arrays vs. pairing
  workspace) and snapshot the split at peak. Metric: `T` = node-array
  bytes / peak bytes. Stage 3 halves node width (8 → 4 B), so its best
  possible peak cut is `T/2`. **Threshold: T ≥ 40%**, i.e. proceed only
  if halving buys ≥ 20% of peak. Model prediction to check against: with
  old-round hash arrays dropped (reconstruction walks nodes only — the
  stage-1 design should not keep dead hashes), nodes ≈ `k·R·8` B vs. two
  resident hash generations ≈ `2·R·(k+1)·cbyte`·-ish; at (200,9) that
  predicts T ≈ 60%+, at k=5 substantially less — so expect the gate to
  pass at high k and fail at low k, and decide on the parameter class
  that matters (production-scale k ≥ 7).
- **(b) Merge-time attribution.** Instrument: the sampling profile per
  `BENCH.md` discipline (same tooling as the 59%-allocation finding),
  attributing merge-phase samples to node-array reads/writes (emit-pass
  stores, any node-chasing in the pairing path). Metric: `M` = fraction
  of merge samples on node-array access. Halving width at best doubles
  node cache-line density, so the plausible time win is bounded by ~`M/2`.
  **Threshold: M ≥ 20%**, i.e. proceed only if the bound admits ≥ 10%
  merge-time gain. Cache-miss counters (Instruments/perf), if available,
  corroborate but don't replace the sample attribution.

Decision rule: stage 3 proceeds if **either** threshold is met at the
parameter class of interest; both misses park it. Either way the measured
T and M get recorded in `BENCHMARK.md` so the decision is auditable.

### 7a.7 The historical constants are guesses, not designs

A caution that governs how §7a.4–7a.5's record should be used. The
published geometries were not optimized fixed points — they were guesses
that survived a handful of hand-tried alternatives: xenoncat tried
2048/1024/512/256 buckets and kept the winner; tromp's 2^16 → 2^12 → 2^10
reductions came from noticing bit headroom (the Cantor-coding budget), not
from mapping the space. No systematic sweep, no cross-parameter
measurement, and no isolation of bucket count from the confounded changes
in the same commits appears anywhere in the record. The numbers are also
mutually incomparable — bucket count only means anything relative to the
pass structure, slot encoding, and cache hierarchy around it (xenoncat's
256 lives inside a 4-pass PARTS design; tromp's 2^10 does not), so the
bundle of interacting choices repriced every constant whenever any one
choice moved. Survivorship got mistaken for design.

Consequences, binding on this repo's backends:

1. **Never inherit a geometry constant** — from the record or from this
   repo's own earlier measurements on other machines. What transfers is
   the arithmetic (§7a.5's feasibility bound, the tail math); what does
   not is every cache-driven and structure-coupled choice.
2. **The dial is measured, not chosen**: T2.4's backend takes bucket
   geometry (b, and slack σ where capacity-capped) as a parameter, and a
   `reqbench` sweep per machine-of-record picks the optimum — recorded in
   `BENCHMARK.md` with provenance, per the hash seam's own
   probe/self-test/measure/freeze pattern. As far as the record shows,
   this would be the first actual measurement of this dial in the
   lineage.
3. **Skipped optimizations are re-derived, not trusted skipped**: the
   same fallibility cuts both ways — decisions the historical authors
   declined may have been miscalculated for their context and may pay in
   ours (and vice versa). Hence the quantified gates (§7a.4's T/M rule)
   in place of judgment calls, and the corpus ports (RZ/RK/CS) so each
   historical bundle can be re-measured in isolation on our hardware.

### 7a.8 What a "bucket" really is, and the uneven-resolution signal

**The bucket array is a hash table with its purpose inverted.** Key: the
digit prefix — an *identity* hash on b bits that are uniformly distributed
by construction (they are hash output), which is why a fixed-capacity
table with σ ≈ 2 slack is viable at all: there is no adversarial key skew
to defend against, a luxury no general-purpose hash table has. Store:
bounded separate chaining (`NSLOTS` inline slots per key). The inversion:
ordinary hash tables are engineered to *avoid* collisions and answer
point lookups in O(1); this table is engineered to *concentrate*
collisions and is never point-queried — it is written once per round and
read back once, as collision groups. Collisions aren't the failure mode,
they are the product being manufactured. tromp's `xhash` early-reject
(`XFULL = 16`) is the same structure applied recursively: a second, tiny
hash table on the RESTBITS *inside* each bucket.

**Uneven complexity resolution.** The bundle resolves its two data axes
to very different standards of rigor:

| Axis | Structure | Standard |
|---|---|---|
| Collision search | prefix-grouped table | deliberately approximate: no total order, overflow rows dropped, duplicate detection deferred to the end (xenoncat) — sloppy wherever failure is rare or detectable |
| Provenance | tree of nodes | fully exact, lossless, resident for all k rounds — never questioned |

The asymmetry is a signal, not a necessity: correctness only requires
provenance to be *reconstructible at solution time*, not *stored exactly
throughout*. And the literature eventually cashed exactly this in —
the post-retrieval techniques 1351 cites as cutting peak memory "a
further factor above two" (`../Equihash.md`) work by not storing full
provenance and re-deriving indices after root detection, i.e. by
resolving the provenance axis down to the same lossy-but-recoverable
standard the search axis always had. Bernstein truncation
(`SECURITY_ANALYSIS.md` §8 item 2) is the same move applied to the row
store itself. Where one part of a pipeline is engineered sloppy and
another is engineered exact, the exact one is the unexamined candidate.

Consequence for this repo: T2.4's backend and T3.1's memory-capped solver
should treat provenance exactness as a **dial** (store-exact ↔
recompute-on-demand), not an invariant — a post-retrieval stage is the
natural stage 4 beyond `pointer.rs`'s current three, gated like stage 3
on measured shares, and doubly interesting here because recompute-based
provenance changes the TMTO surface T3.1 exists to measure.

### 7a.9 Transfer to the Sequihash problem statement

How §7a.1–7a.8 map onto the repaired problem (RGBP — k lists, one element
per list — versus the loose single-list LGBP Equihash actually solves;
`../Equihash.md`, SIZING §0).

**What transfers unchanged.** The collision-harvesting hash table
(§7a.8), incomplete sort, and static allocation are class-agnostic: they
operate on digit bits, whose uniformity is a hash-level property the
regularity constraint doesn't touch. §7's note stands — in Requihash
these are safe *and* wanted, because the algorithm they accelerate is the
k-list one, which an ASIC cannot improve by the single-list trick (the
paper: index pointers there at least *double* memory, a 12× penalty at
(200,2^9) for an ASIC attempting it).

**What the problem statement disables, precisely.** Not tree-structured
bookkeeping per se — this repo's `pointer.rs` legitimately uses a
provenance tree for the client-side merge — but the *cross-position
memory collapse*: in LGBP all `2^k` solution positions draw from one
pool, so one stored representation serves every position. RGBP's class
constraint (position i ⇒ class i mod k) makes positions
non-interchangeable, which is what breaks the single-list scheme's
economy and leaves Proposition 3's *index trimming* as the surviving
provenance optimization.

**One principle at three scales.** Index trimming is the
problem-statement-level instance of the same move as §7a.4's Cantor rank:
*spend no bits on distinctions the structure already makes.*

| Scale | Structure exploited | Saving |
|---|---|---|
| Pair encoding (§7a.4) | pairs are unordered, distinct | 1 bit/node (Cantor rank vs bitfield) |
| Wire encoding | class implied by solution position | 1 bit/index: minimal `ell+1` → compact `ell` bits; 1344 → 1280 B at (200,9), measured |
| Solver provenance (Prop 3/6) | class implied by position + tree shape | the trimmed-index term `2^(k−1)` *bits* per item in Prop 6 — orders below naive index tuples |

The compact solution encoding is Requihash's own "Cantor move," made at
design time rather than optimization time.

**Approaches to optimizing Sequihash solutions**, organized by axis:

1. *Search axis*: the transferred 2016-17 toolbox (above) plus the
   geometry-as-measured-dial discipline (§7a.7) — nothing
   Sequihash-specific, all already tracked (T2.1, T2.4, T2.5).
2. *Provenance axis*: index trimming natively; then the
   uneven-resolution correction (§7a.8) — post-retrieval / recompute — is
   the frontier. The companion paper 2141 is this direction executed for
   Equihash(144,5): 0.57× of tromp's footprint at ~1× time, 0.28× at ~2×
   time (SIZING §2's fourth data point). Whether its techniques carry to
   the k-list setting is exactly the kind of claim this repo re-derives
   rather than trusts (§7a.7 consequence 3).
3. *Problem-statement axis*: the `m` dial (SPEC.md §5–6) — Sequihash's
   own verification-cost/memory knob, orthogonal to solver engineering.
4. **The security identity**: every provenance-axis optimization *is* a
   time-memory tradeoff move — the optimizer's toolbox and the attacker's
   toolbox are the same set. For Equihash that identity was the collapse
   (the "optimization" was the attack). Sequihash's claim is that its
   tradeoff is steep (penalty ~q^(k/2) under algorithm binding), so
   optimizing honest solvers and measuring attack steepness are *the same
   experiments* — which is why T3.1's counting harness and the solver
   work share instrumentation, and why a stage-4 post-retrieval solver
   doubles as T3.1's first data point on the claim.

### 7a.10 Cache micro-architecture: the unexamined dimension

The record's cache awareness stops at *capacity* — tromp shrank the
counts array to fit cache, xenoncat's `PARTS=4` is capacity blocking.
Nothing in the record considers the rest of the cache structure, and
power-of-2 habits actively work against part of it:

- **Power-of-2 strides vs. set associativity.** Power-of-2 bucket counts
  and array strides are chosen for shift/mask arithmetic, but in a
  set-associative cache, accesses at power-of-2 strides concentrate into
  few sets — the classic conflict-miss pattern. The scatter pass is
  precisely such an access stream. No associativity analysis appears
  anywhere in the record; mitigations (skewed indexing, per-bucket
  line-offset staggering, 2^b ± prime table sizes at the cost of a
  modulus) are unexplored. Line-boundary alignment of slots is likewise
  accidental — row widths are odd byte counts that straddle lines unless
  padded, and padding spends the very memory being optimized: a tension
  to *measure*, not resolve by taste. Per-arch datum that changes the
  answer: Apple Silicon lines are 128 B, x86's 64 B — the same layout
  cannot be optimal for both (`../BLAKE/Platforms.md` is the hardware
  reference home).
- **Sequential access during rounds.** The scatter is a random-write
  stream by construction. Software write-combining (accumulate a line's
  worth per bucket group before flushing) and multi-pass radix splits
  (each pass's working set sized to L2) are the standard remedies; only
  the coarse second exists in the record (PARTS).
- **Row layout for reuse — lifetime segregation.** Hash bytes die at
  round end; tree nodes live to reconstruction (§7a.1). Storing them
  interleaved (as this repo's prototype `Row { hash, ptr }` does) drags
  dead hash bytes through every later-round cache line. Production
  stage 1 should segregate the stores (SoA by lifetime, not just by
  field) so resident lines contain only live data.
- **In-place update is also a cache optimization.** Technique 4 is argued
  in §7a.6 as a capacity win, but overwriting freed input slots also
  *reuses resident lines* instead of faulting fresh ones — its time
  effect ("~neutral, possibly small gain") may be understated on
  bandwidth-bound rounds; measure both axes.
- **Shuttle scheduling (LIFO — last written, looked at first).** Round
  structure forbids full depth-first processing (a merge output's
  next-round bucket is data-dependent), but temporal locality is still
  schedulable: pair within a bucket in reverse scatter order while its
  lines are hot; and fuse the *next* round's counting pass into the
  current round's emit (the output row's next digit is known at emit
  time), deleting one full cold sweep per round. The fused count is the
  most concrete unclaimed win in this list.

All of these are candidate *experiments* under §7a.7's
measured-not-inherited discipline — per-arch, via the `reqbench` sweep,
alongside T2.4/T2.5 — not defaults to adopt on argument.

### 7a.5 Bucket number vs. bucket size: the calculation

Terms (collecting §7a.1–7a.4's notation in one place): **R** = rows in the
round's list (≈ 2^(ell+1)); **W** = the pair-word width in bits (32
everywhere in the record); **b** = bucket-id bits, so B = 2^b buckets;
**s** = slot-id bits, so per-bucket capacity NSLOTS = 2^s; **μ** = R/B =
mean occupancy; **σ** = slack factor = NSLOTS/μ (capacity headroom over
the mean); **rowsize** = bytes stored per slot (hash remainder + tree
node).

1. **Feasibility** — capacity must cover the mean occupancy with slack:
   `2^s ≥ σ·μ = σ·R/2^b`, i.e. `s + b ≥ log2 R + log2 σ`. The word
   budget spends the same bits: `b + 2s = W` (bitfield). Substitute
   b = W − 2s into the first inequality:
   `s ≤ W − log2 R − log2 σ`. Reading: every extra slot bit costs two
   word bits but only frees one bucket bit, so slot bits are the scarce
   resource, bounded by how big R is. At (144,5): R = 2^25, σ = 2 →
   s ≤ 32 − 25 − 1 = 6 — RZ's exact choice (6 slot bits, 20 bucket
   bits). At (200,9): R = 2^21 → s ≤ 10 — tromp's exact choice.
   Triangular encoding relaxes the budget by one bit (§7a.4).
2. **Within the feasible set, geometry doesn't change total memory**:
   slot storage = B·NSLOTS·rowsize = `2^(b+s)·rowsize = σ·R·rowsize`
   regardless of how b and s split. So the choice is decided by three
   *other* effects, each worth spelling out:
   - **Counts-array cache residency**: the count/prefix/scatter passes
     random-access a `2^b`-entry array (4 B each). At 2^20 buckets that
     array is 4 MB — larger than L2 on the machines of the record — and
     every scattered row risks a cache miss on it; at 2^10 it is 4 KB,
     L1-resident. This is the concrete meaning of "fewer buckets are
     faster to scatter into" and the actual driver behind tromp's two
     bucket-count reductions.
   - **Tail behavior**: at fixed σ, the overflow probability rises as μ
     falls — Poisson concentration is relative to √μ, so small-μ buckets
     overflow their σ·μ cap proportionally more often. Fewer, fuller
     buckets are statistically safer at the same slack.
   - **Digit-prefix limit**: the bucket key is the leading b bits of the
     round's ell-bit digit, so b ≤ ell; whatever b leaves over
     (RESTBITS = ell − b) the in-bucket scan must still compare
     (§7a.2). Larger b does more of the grouping in the scatter; smaller
     b pushes work into the per-bucket scans.
3. **Slack policy** — σ is set by tolerable loss, not guessed: expected
   dropped rows per round ≈ `2^b · P(occ ≥ σμ) · E[excess]`, with the
   Chernoff bound `P(Poisson(μ) ≥ σμ) ≤ e^(−μ(σ·ln σ − σ + 1))`. σ = 2
   at μ ≥ 32 gives order-single dropped rows per round of tens of
   millions — the historical sweet spot; smaller μ needs larger σ for the
   same loss rate, which is the tail-behavior point above from the other
   direction.

### 7a.6 What completing techniques 3-4 would buy here (estimates)

Memory and time effects kept strictly apart — the two do not trade against
each other here; each technique has one primary axis and one secondary.

**Peak-memory impact** (against `SIZING.md` §1c's full-index model,
`N·(3·cbyte + 12·2^(k−1) + 96)` B):

| Technique | Mechanism | Peak effect |
|---|---|---|
| (3) static merge allocation | out-buffers sized once (exact pre-pass, §7a.3) — no `Vec` growth-doubling slack | removes the main residual *above* the model (measured 0.87–1.51× of model → ~1.0×): typically **20–35% lower peak**, and the model becomes near-exact for T2.2's gating predictions |
| (4) in-place merge | output overwrites freed input slots — only one generation resident instead of two | dominant term `12·2^(k−1)` → `8·2^(k−1)`: **~33% cut**; at (200,9), model 6.2 → ~4.2 GB |

**Solve-time impact** (against `BENCHMARK.md` §6's profile; the "59%" is a
*time-profile share* — fraction of CPU samples in allocation — not a
memory quantity):

| Technique | Mechanism | Time effect |
|---|---|---|
| (3) static merge allocation | no realloc-copies during output growth; no allocator calls in the round loop | est. **5–15% faster solve** (the arena already removed the per-row-allocation share of the 59%; this removes the remaining growth-realloc share); larger effect under T2.1's parallel merge, where per-task growth contends on the global allocator |
| (4) in-place merge | fewer writes, better locality | **~neutral**, possibly small gain; not the reason to do it |

**Sequencing caveat**: (3) is representation-independent — worth doing
regardless of T2.4, and it tightens T2.2's model. (4)'s absolute win
collapses once T2.4's pointer rows exist (the `2^(k−1)` index term it
saves a third of is *entirely deleted* by pointers), so (4) is sequenced
after T2.4 and re-estimated then; it may only be worth keeping for the
full-index baselines.

Tracked as `PLAN.md` T2.5.

## 8. Measurement discipline: `reqbench`, shared across `Req/rust` and `SOLVER_CORPUS`

Any timing or memory number reported anywhere in this tree — `Req/rust`'s
own benches or a `SOLVER_CORPUS` port's — follows one discipline, specified
in [`BENCH.md`](BENCH.md): repeated trials with min/median/MAD statistics
(never a single sample), git-commit/dirty-tree provenance stamped on every
record, and peak-memory figures cross-checked against a second, independent
instrument (OS-reported RSS) before being trusted — not because any one of
these is exotic, but because each closes a real gap this project hit once:
RZ's first bench pass (`SOLVER_CORPUS/rz/src/bin/rz_bench.rs`, an earlier
session) reported single-sample timings with no provenance and a memory
figure checked against OS RSS exactly once, by hand, rather than as a
standing part of the tool.

`Req/rust`'s own `report.rs` (statistics/JSON-lines/baseline-comparison)
predates this and stays as its own, intentionally separate implementation
— `Req/rust` has no dependency on anything under `SOLVER_CORPUS/`, and
coupling it to a directory whose whole design point (`SOLVER_CORPUS.md`'s
own cross-cutting requirements) is standalone-per-port ports would cut
against that design. Instead, `SOLVER_CORPUS/reqbench/` is a **new,
separate, dependency-free crate** — a generalized extraction of
`report.rs`'s statistics logic (its identity key is a plain string, not a
hardcoded `(n,k)` pair, since a port's natural key is `(WN,WK,RESTBITS)` or
similar) plus the provenance-stamping and memory-cross-check pieces
`report.rs` never had. Every `SOLVER_CORPUS` port depends on `reqbench` via
a relative path; `Req/rust` does not depend on it and is not required to.
`SOLVER_CORPUS/_template/` gives a new port (RK/RT/CS) the harness shape to
start from, so each doesn't rediscover what RZ's second pass already
worked out.

**Next, in priority order:**

1. **Compact index-pointer storage (technique 1).** The one canonical 2016-17
   technique still not in `all_solvers()`. Design proven correct in
   `rust/src/solve/pointer.rs` (a prototype: plain sort instead of
   `bucket.rs`'s counting sort, cross-round pointer-tree reconstruction
   verified against `solve_reference` and the production verifier). What
   remains: fold in the counting-sort merge, register as `solve::pointer`
   in `all_solvers()`, gate on `all_solvers_agree` plus the A14 KAT vectors,
   measure real peak memory with `req_memcheck`. Biggest remaining memory
   lever, and what makes production (200,9) mining feasible.
2. **In-place bucket merge (technique 4).** Fold the round output back into
   the working buffer to drop the per-round out-buffer allocation.
3. **Bucket-parallel merge (Tier 2).** Now that buckets are explicit, each
   bucket is an independent unit of work — the natural parallel decomposition
   rayon's generation-only solver could not reach.
