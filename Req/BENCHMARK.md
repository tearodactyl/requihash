# BENCHMARK.md — Requihash reference performance and profile

Measured on Apple Silicon (aarch64), scalar reference backend, `--release`
(opt-level 3, LTO, 1 codegen unit). Reproduce with:

    cargo run --release --manifest-path rust/Cargo.toml --bin req_bench
    # profiling:
    cargo build --release --manifest-path rust/Cargo.toml --bin req_profile
    rust/target/release/req_profile &  ; sample <pid> 5 -f prof.txt ; kill %1

Evidence grade: Measured (harness committed as `rust/src/bin/req_bench.rs` and
`req_profile.rs`).

## 1. Headline numbers

| Param | init list | leaf-hash | full solve | gen/merge split | verify |
|---|---|---|---|---|---|
| (48,5) | 512 | 0.20 ms | 0.36 ms | 24% / 76% | 6.9 us |
| (72,5) | 8,192 | 2.56 ms | 7.60 ms | 15% / 85% | 7.1 us |
| (96,5) | 131,072 | 19.1 ms | 144.3 ms | 13% / 87% | 7.1 us |

Leaf list grows 16x per step; solve time grows ~19-21x — superlinear, so the
merge (not hashing) is what scales badly.

## 2. Two findings that overturn the naive assumption

**Finding 1: merge dominates, not hashing.** The intuition (and an earlier note in
ARCHITECTURE.md) was that BLAKE2b leaf generation is the miner's dominant cost,
so batched-SIMD hashing is the big win. The `gen/merge` split says otherwise:
generation is 24% at (48,5) and *falls* to 13% at (96,5) as the list grows. The
merge — sort plus pairwise bucket handling — is 76-87% and rising. Batched SIMD
hashing would speed up only the shrinking 13% fraction.

**Finding 2: allocation dominates the merge, not arithmetic.** A `sample` profile
of a (96,5) solve loop attributes leaf time as:

| Cost centre | share of top-3 | what it is |
|---|---|---|
| malloc/free | 59% | one `Vec<u8>` + one `Vec<EhIndex>` allocated *per merged row* |
| sort | 24% | `sort_by` on the collision segment each round |
| BLAKE2b compress | 17% | actual leaf hashing |

The reference solver allocates two fresh heap vectors for every merged row, and
there are ~130k rows per round times 5 rounds — millions of tiny allocations per
solve. That is the true bottleneck. It is an artifact of the correctness-first
reference design (`solver.h` / `lib.rs` `solve`), not of the algorithm.

## 3. What each finding implies for optimization

The optimization order the data dictates — highest payoff first:

1. **Kill per-row allocation (59%).** Store rows in a flat arena: one contiguous
   `Vec<u8>` of fixed-stride slots for hashes, one `Vec<u32>` for indices, and
   index into them. This is exactly what the index-pointer / index-trimming
   k-list solver (paper Prop. 3, Equihash.md F-A1) does — and note the irony: the
   index-pointer representation that collapsed *Equihash's* ASIC resistance is,
   for *Requihash*, just the standard way to make the regular k-list solver
   memory-efficient without weakening it, because the regularity binding blocks
   the single-list attack the representation once enabled (F-A4).
2. **Reduce sort cost (24%).** Radix/bucket sort on the 1-2 byte collision segment
   instead of comparison `sort_by`; the key is tiny and bounded, ideal for radix.
3. **Batch the hashing (17%).** Only now does `blake2b_simd hash_many` matter, and
   its ceiling is a 17% -> near-0 improvement on this path — real but third in
   line. It rises in importance once (1) and (2) are done and hashing is a larger
   fraction of the shrunk total.

This inverts the ARCHITECTURE.md tiering emphasis: the first two wins are *solver*
(Seam B) improvements, not *hash* (Seam A). The mix-and-match structure still
holds — both are seams — but the profile says spend the first effort on Seam B.

## 4. The verifier is already fast and flat

Verify is ~7 us regardless of n, because it always processes just `2^k = 32`
leaves through k fixed rounds. At ~140k verifies/second single-threaded, the
consensus-critical path is not a bottleneck and needs no acceleration — which
vindicates keeping it as the scalar, portable, non-seam reference
(HardwareBridge.md "verification must stay boring"). A full node validating
blocks spends negligible time here.

## 5. C++ vs Rust, and the optimization it drove

Both implementations run the identical algorithm (same per-round list sizes to the
row) so the comparison isolates language/runtime, not algorithm. Built with matched
max-opt flags: C++ `-O3 -flto -mcpu=native`, Rust `opt-level=3 + LTO +
codegen-units=1 + target-cpu=native`.

| metric | C++ | Rust | winner | gap |
|---|---|---|---|---|
| leaf-hash (96,5) | 7.72 M/s | 6.60 M/s | C++ | 17% |
| leaf-hash (72,5) | 3.75 M/s | 3.26 M/s | C++ | 15% |
| solve (96,5) | 163.1 ms | 152.4 ms | Rust | 7% |
| solve (72,5) | 8.53 ms | 8.22 ms | Rust | 4% |
| verify (96,5) | 8.8 us | 7.6 us | Rust | 16% |
| verify (48,5) | 8.5 us | 7.2 us | Rust | 18% |

The split is diagnostic, not noise:

- **C++ wins leaf hashing (~15-17%)** — tighter codegen on the fixed-size BLAKE2b
  compress loop.
- **Rust wins end-to-end solve (4-7%) and verify (16-18%)** despite the slower
  hash. Solve is 87% merge and merge is 59% allocation, so Rust's allocator and
  `Vec` move semantics beat C++ `std::vector` churn by enough to overcome the hash
  deficit. Verify — pure per-row vector allocation, negligible hashing — isolates
  that gap cleanly at 16-18%.

**The comparison localized the bottleneck to allocation, and that drove the fix.**
Both languages were losing the same place: per-row heap allocation in the merge.
The arena solver (`solve_arena`, struct-of-arrays flat buffers, sort a u32
permutation instead of moving rows) removes it:

| param | reference solve | arena solve | speedup |
|---|---|---|---|
| (48,5) | 0.29 ms | 0.19 ms | 1.52x (-34%) |
| (72,5) | 7.95 ms | 4.98 ms | 1.60x (-37%) |
| (96,5) | 151.8 ms | 93.5 ms | 1.62x (-38%) |

A measured **1.6x** across all params, exactly where the profile (59% malloc) and
the language comparison (Rust's allocator win) both pointed. `solve_arena` is
cross-validated against `solve` on 80 nonce cases at two parameter sets
(`arena_matches_reference` test) and every arena solution verifies, so the speedup
costs nothing in correctness. The same arena change ported to C++ is expected to
close its solve gap and likely retake the lead given its hashing advantage — the
obvious next round.

## 6. Round 2: all backends behind the seams

After refactoring into the seam structure (ARCHITECTURE.md), every tier now has
working examples in both languages, benchmarked on identical params. All are
cross-validated: `all_solvers_agree`, `all_verifiers_agree`, `arena==reference`
(C++), and the SIMD hasher passes the self-test gate (`simd_hasher_matches_scalar`).

**Solvers (Seam B), (96,5) median ms:**

| solver | Rust | C++ | speedup vs reference |
|---|---|---|---|
| reference | 149.9 | 161.8 | 1.00x |
| arena | 94.6 | 104.4 | Rust 1.58x, C++ 1.55x |
| bucket (2016-17 incomplete sort) | 80.0 | — | Rust 1.86x |
| parallel (rayon gen) | 78.5 | — | Rust 1.91x |

The `bucket` solver applies the tromp/xenoncat 2016-17 incomplete-bucket-sort
technique (counting sort on the collision digit, O(m), replacing the O(m log m)
comparison sort that was 24% of solve). It is 14% faster than arena and beats the
generation-parallel rayon solver single-threaded. See
[OPTIMIZATION_HISTORY.md](OPTIMIZATION_HISTORY.md) for the full 2016-17 technique
mapping and what remains (compact index-pointer storage, the (2^k)/k space win).

**Verifiers (verify seam), (96,5) us/verify:**

| verifier | Rust | C++ |
|---|---|---|
| reference | 7.4 | 8.4 |
| arena | 7.1 | — |
| early-reject | 5.7 | 6.1 |

**Hashers (Seam A), batched hash_many, M leaves/s (Rust):** scalar 8.25 -> simd
9.59 at (48,5) (+16%), converging to ~parity at (96,5) as memory bandwidth, not
compute, bounds the batch.

### What round 2 established

1. **Arena is a real, portable win: ~1.55-1.58x in *both* languages.** The
   optimization the profile predicted transfers across languages nearly identically,
   confirming it is an algorithmic/memory property, not a runtime artifact.

2. **C++ did NOT retake the solve lead — Rust stays ~9% ahead even at arena parity.**
   Round 1 predicted C++'s 15-17% hash advantage would let it win once allocation was
   equalized. It didn't: at (96,5) hashing is only ~12% of solve, so a 16% hash edge
   is ~2% of total, swamped by Rust still winning the sort+merge. The prediction was
   wrong and the data says why — the hash advantage is too small a fraction to matter
   at these params. (It would matter more at small n where gen% is higher.)

3. **early-reject verifier is the best verify backend: 23-27% faster** than the
   reference in both languages, by tracking index sub-ranges instead of cloning index
   vectors. The verifier was already cheap (~7 us); this makes it ~5.7 us. Still not a
   bottleneck, but it is the version to ship.

4. **Parallel (rayon) gives 1.9x by parallelizing only leaf generation** — and that
   is the ceiling of gen-parallelism, since the merge is still serial. The next
   parallel win requires bucket-partitioning the *merge* across threads, which is
   harder (the round barrier and cross-bucket collisions).

### Next round of improvements, in priority order

The data now points past the arena win to three concrete next steps:

1. **Radix-sort the merge (Seam B).** The profile's 24% sort cost is still comparison
   `sort_by` on a 1-2 byte key — a textbook radix-sort candidate. Expected to shrink
   the now-dominant sort fraction of the arena solver. Highest payoff.
2. **Bucket-parallel merge (Tier 2).** rayon's 1.9x is generation-only; partitioning
   the merge by collision-prefix bucket across threads is the way past it. Each round
   is a parallel-over-buckets map with a barrier.
3. **SIMD hash matters only at small n or after (1)+(2).** At (96,5) it is ~parity;
   its value rises once the merge is parallelized and hashing reclaims a larger share,
   or at parameters where gen% is high. Keep it behind the `simd` feature as the
   self-test-gated option it is.

The arena solver should become the default; the early-reject verifier should become
the default verifier; the reference solver/verifier stay as the equivalence oracle.

## 7. Caveats

- aarch64 only; x86 AVX2 numbers would differ, especially the hash fraction
  (wider SIMD helps hashing but not the allocation-bound merge). The `simd`
  backend uses `blake2b_simd`, which ships AVX2 on x86 and portable on aarch64,
  so the +16% batched-hash win is expected to be larger on x86.
- Small parameters. At production (200,9) the list is 2^21 and the allocation
  problem is strictly worse, so the arena win strengthens at scale.
- gen% is measured with scalar BLAKE2b in both phases; swapping in the SIMD hash
  lowers gen% further, making the merge share (and thus radix/parallel-merge
  priority) even more dominant.

## 5. Harness fitness for per-component iterative optimization (reviewed)

The requirement: benchmark and iteratively optimize each algorithmic component — the generator hash (BLAKE2b today, BLAKE3 candidate) — separately and in composition. Verdict on this harness, and the upgrade order.

**What is already fit.** The `hash::LeafHasher` trait with scalar/SIMD backends, `agrees_with_reference` differential checking, and `autodetect` is exactly the seam component iteration needs: a candidate backend drops in, is proven equivalent, then benched. The solver (`reference`/`arena`/`bucket`/`parallel`) and verifier (`reference`/`arena`/`early`) backend families give the same seam at the composite level, and the instrumented solve's gen/merge split with per-round list sizes attributes composite time to the hash component directly. `bench_leaf_hash`'s ns/leaf and leaves/s are the right component metrics.

**Gaps, in the order they block iteration:**

1. **The BLAKE3 construction is unspecified — the code is trivial.** The `blake3` crate is mature and validated; what does not exist is the byte-exact construction: `derive_key` context contents, one-XOF-call-per-leaf vs stream, index placement (the XOF is O(1)-seekable — output block i computes directly from the root chaining value, keeping per-leaf recomputation flat for TMTO accounting, but only if specified that way), midstate strategy. Write the construction into the family specification (below), then implement `hash/blake3.rs` behind an optional feature.
2. **No primitive-level bench, and separation must respect the caches.** Component stack: hash primitive (register work) → leaf assembly (midstate clone, index injection, finalize, slice, pack) → generation phase (plus real arena/bucket writes). Separation is by **substitution, not in-loop timers** (timers perturb the caches under measurement): run the phase in code-shape-identical variants — real, hash-stubbed, assembly-stubbed — and attribute by marginal deltas. Micro benches write through the real row-sink stride (a reused buffer fakes D-cache locality). The sum of marginals will not equal the phase total; **the residual is reported as the interaction term**, not forced to zero — it is the cache-contention measurement. Hardware counters (L1i/L1d misses) cross-check, never lead.
3. **No regression discipline.** Statistics level, justified by the decisions served (accept/reject a change; drift across commits; D3 effect sizes): deterministic counters stay primary (noise-free); the wall-clock layer reports **min + median + dispersion (MAD)** over warm runs — for a deterministic kernel, noise is additive so the minimum estimates true cost — with the decision rule *a win must exceed the dispersion band on minima*. No t-tests or bootstrap CIs on micro-timings (non-iid, skewed — pseudo-rigor); heavier statistics belong only to macro node runs (ZeroPerf tier). Emit JSON lines, diff against a committed baseline file.
4. **No `m` (iteration) axis.** A pure parameter of the function under test: plumb as a `Params` field (it changes the function's identity — it belongs in domain separation) plus a generic `Iterated<H>` adapter over any `LeafHasher`, so every hash backend gets iteration for free. Chain-side packaging (eras) is irrelevant to the lab.
5. **Parameter ceiling — implementation-specific, not a framework property.** Generation and verification benches run at any parameters today (generation is linear; verification is `2^k·m` hashes) — so hash comparisons are answerable at full production parameters now, for exactly the two phases where the hash lives. Only the composite full-solve at large parameters waits on compact index-pointer storage.

**Segregation from node development.** This lab is materially independent of Zebro/Zebra (no dependencies on either; std-only), and the work continues here — `Requihash/Req`, already under version control, with `SPEC.md` (the family specification) and `baselines/` (committed bench results) as the only structural additions. A dedicated repository is deferred until a trigger fires: a publication boundary between this implementation and the essay corpus, lab CI, a second consumer beyond Zebro, or history-navigation cost; `git subtree split -P Req` preserves full history whenever that day comes. The artifact that makes the segregation crisp is the **family specification** owned by this program: the parameterized object `PoW(n, k, hash, m, keying ∈ {single-list, regular}, context)` with byte-exact generator, solution encoding, and verification algorithm. Every element — Requihash's keying included — is a proposal and a design freedom; the lab searches that space with measurements, and the chain side pins one instance as era data behind its engine seam when the search converges. Node development and PoW research couple only through the spec and a vector-file format.

**Composition across levels.** This harness owns solver-side composition; the consensus-stack verify yardstick (Zebro's criterion bench over the pinned `equihash` crate) and the in-node C++ measurement (ZeroPerf) are the other two levels. They are deliberately not seamed — they measure shipped stacks. The bridge: this harness (variant set to plain Equihash) can *mine* cross-parameter solution vectors for the consensus-side verify curve. BLAKE3 composition can only be measured here until an owned PoW crate exists — no shipped stack carries it.
