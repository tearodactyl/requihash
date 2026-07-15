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
auditable, because it is the consensus-critical path (HardwareBridge.md: "verification
must stay boring"). A miner may be exotic; a verifier may not.

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
  hardware tier (F-X1's convergent MAC substrate, GPU/FPGA offload) and the C++
  side. The trait keeps that door open at the cost of one indirection.

The structure costs one function-pointer indirection per *batch* (not per leaf)
and one startup detection. In exchange, the same core encoding + verifier +
difficulty logic is written once and every acceleration tier — present and future,
CPU SIMD through the inference-hardware MAC arrays of HardwareBridge.md F-X1 —
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
