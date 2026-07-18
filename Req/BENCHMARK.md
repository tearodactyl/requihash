# BENCHMARK.md — Requihash reference performance and profile

Measured on Apple Silicon (aarch64), scalar reference backend, `--release`
(opt-level 3, LTO, 1 codegen unit). Reproduce with:

    cargo run --release --manifest-path rust/Cargo.toml --bin req_bench
    # machine-readable records + baseline tracking (see "Regression workflow"):
    cargo run --release --manifest-path rust/Cargo.toml --bin req_bench -- \
        --json baselines/<tag>.jsonl --tag <tag>
    cargo run --release --manifest-path rust/Cargo.toml --bin req_bench -- \
        --baseline baselines/<tag>.jsonl
    # profiling:
    cargo build --release --manifest-path rust/Cargo.toml --bin req_profile
    rust/target/release/req_profile &  ; sample <pid> 5 -f prof.txt ; kill %1

Evidence grade: Measured (harness committed as `rust/src/bin/req_bench.rs`,
`req_profile.rs`; statistics and comparison rules in `rust/src/report.rs`).

**`report.rs`'s statistics/provenance/comparison design has since been
extracted, generalized, and extended into a standalone crate,
`SOLVER_CORPUS/reqbench/`** (`Req/BENCH.md`, `ARCHITECTURE.md` §8) — used
by every `SOLVER_CORPUS` port (RZ today; RK/RT/CS as they're built) so each
doesn't reinvent this discipline per port. `report.rs` here in `Req/rust`
stays its own, intentionally separate implementation rather than being
retrofitted onto `reqbench` — `Req/rust` has no dependency on
`SOLVER_CORPUS/`, by design. The two share a common statistical method
(min/median/MAD, the same regression band rule) but are not the same code;
if the two drift in behavior, treat that as expected, not a bug, unless a
specific inconsistency is found worth reconciling.

**Regression workflow** (the §8 gap-3 discipline, implemented): every bench
emits a JSON-lines record (min/median/MAD over warm reps; warm-up by time
budget so microbenches get scheduled and ramped before sampling). A baseline
file holds several appended runs; comparison is against the per-key *median of
run-minima* with the decision band max(MADs, 5% of baseline) — the floor was
set empirically: same-code cross-process spread on an unpinned laptop reaches
~5-7%, far above within-run MAD, and best-of-N baselines are wrong-shaped
(typical runs sit above a best-ever floor, flagging phantom regressions).
Resolution tiers that follow: within-process seam comparisons (backends
interleaved in one run) resolve ~1%; cross-process baseline tracking resolves
the band; any verdict at the band edge counts only if it reproduces on a second
run. Single-rep records (the instrumented solve's phase splits) are never
tracked — their ratios are the datum. Committed baselines live in
`baselines/`, tagged per machine.

**Collection hygiene:** baselines recorded on a loaded machine are biased
slow and manifest as phantom across-the-board wins — collect them idle (no
concurrent builds), let the machine cool between back-to-back heavy runs (a
family run launched immediately after another produced a ~19% thermal
outlier on the blake2b family while blake3 held stable), and treat a
comparison where *unrelated* benches all move the same direction as a
machine-state signal, not a result.

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
(the "verification must stay boring" consensus-path principle). A full node validating
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
[ARCHITECTURE.md](ARCHITECTURE.md) §7 for the full 2016-17 technique mapping
and what remains (compact index-pointer storage, the (2^k)/k space win).

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
  so the +16% batched-hash win is expected to be larger on x86 — but *which*
  x86 matters (§7a): not every Intel chip in the field actually has AVX-512,
  and AVX2 itself is universal only from Haswell (2013) forward.
- Small parameters. At production (200,9) the list is 2^21 and the allocation
  problem is strictly worse, so the arena win strengthens at scale.
- gen% is measured with scalar BLAKE2b in both phases; swapping in the SIMD hash
  lowers gen% further, making the merge share (and thus radix/parallel-merge
  priority) even more dominant.

### 7a. Intel generation-to-ISA map, for whenever an x86 leg is run

The full Intel-generation → ISA timeline (which silicon has SSE/AVX2/
AVX-512, the Alder-Lake fuse-off, the per-core hybrid complication, and
why **AVX2 is the realistic universal x86 SIMD floor** for consumer
hardware) now lives in **[`../BLAKE/Platforms.md`](../BLAKE/Platforms.md)
§2** — the single home for x86 instruction-family detail, so it isn't
duplicated and drifting across docs.

The one fact §7 needs inline: **"x86 has AVX-512" is not a safe blanket
assumption** — AVX2 is universal only from Haswell (2013) on, and
AVX-512 is fused off on current Intel client chips — so an x86 SIMD leg
should benchmark against the **AVX2** tier as the floor, and treat
AVX-512 as opportunistic. A runtime feature check reports the truth per
CPU, so no code special-cases it.

## 8. Harness fitness for per-component iterative optimization (reviewed)

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

## 9. Family campaign — first results (blake2b vs blake3 × m)

Apple Silicon, `--release --features blake3`, regular keying, per SPEC.md
constructions; records in `baselines/apple-silicon-dev.jsonl` (`gen/*`,
`gensub/*`, `verify_cost/*`). Evidence grade: Measured.

**Generation phase (ns per leaf-unit; leaf-units = leaves × m; blake2b_simd
column from the implementation-matched rerun, median of two agreeing runs with
one thermal outlier discarded per the confirmation rule):**

| Config | blake2b (bundled scalar) | blake2b_simd (batched hash_many) | blake3 | blake3 vs best blake2b |
|---|---|---|---|---|
| (96,5) m=1 | 139–145 | 124.9 | 77.0 | 1.62× |
| (96,5) m=4 | 121–125 | 116.0 | 96.5 | 1.20× |
| (96,5) m=16 | 116–119 | 108.6 | 86.0 | 1.26× |
| (144,5) m=1 | 145–155 | 137.7 | 93.8 | 1.47× |
| (200,9) m=1 | 155–164 | 146.3 | 110.0 | 1.33× |

Three readings. (1) blake3's m=1 advantage comes substantially from its
*streamed* generation (one XOF stream per class — no per-leaf hasher
setup). (2) Iteration narrows it: m ≥ 2 falls back to per-leaf work and the
advantage drops to ~1.3×, while blake2b's per-unit cost *drops* with m
(iteration rounds absorb one short block, no input/nonce prefix) — the
`blake3` path caches the `derive_key` context template once and clones it
per round rather than re-deriving it per leaf, which is what keeps its
iterated cost this low. (3) The full (144,5) initial list generates in
seconds, not minutes — production-parameter generation benching is
routine.

**Implementation-matched verdict (ARM).** `blake2b_simd`'s batched
`hash_many` generation (byte-equivalence-gated against the scalar
construction) gains only ~10% over the bundled scalar backend here, because
that crate's vector paths (`avx2.rs`, `sse41.rs`) are x86-only — on
aarch64 it falls back to `portable.rs`. Against the best available blake2b
measured in this repo, blake3 keeps 1.33–1.62× at m=1 and ~1.2–1.3× under
iteration (the pre-stated R ≤ 0.8 gate is met everywhere except (96,5) m=4
at R = 0.83, marginal).

**The blake3 numbers above are NEON-accelerated**, confirmed directly
(`blake3::platform::Platform::detect()` returns `NEON` on this machine):
the `blake3` crate's build script enables its NEON C intrinsics
automatically on any aarch64 target (`is_aarch64() && is_little_endian()`,
no feature flag). BLAKE2b has a comparable NEON implementation too — the
official `BLAKE2/BLAKE2` repo ships a maintained `neon/blake2b-neon.c` (icebox — optimization-stage work this program is not at yet; `BLAKE/BLAKE.md` §5.2, PLAN A13) with
a dedicated aarch64 makefile (local clone:
`~/Work/ZK/ZKs/BLAKE/blake2-reference`) — but no published Rust crate wraps it
the way `blake3` wraps its own; the gap is packaging, not the existence of
BLAKE2b NEON code. `blake3`'s automaticity comes specifically from its
`build.rs` shelling out to a C compiler at build time and setting
`cargo:rustc-cfg=blake3_neon` with zero user action — `blake2b_simd` has no
`build.rs` at all, so there's no equivalent hook to extend with a NEON
branch; closing this gap needs a genuine Rust translation of
`blake2b-neon.c`'s C intrinsics to `core::arch::aarch64`, not an FFI
wrapper. Tracked in PLAN.md A13, which carries the full technical summary.
A separate x86-64/AVX2 leg (where `blake2b_simd`'s 4-way path should narrow
the gap) is deprioritized, PLAN.md A7.

**Substitution attribution at (96,5), m=1:**

| | real | hash marginal | assembly marginal | residual |
|---|---|---|---|---|
| blake2b | 17.79 ms | 79% | 21% | 0% |
| blake2b_simd | 20.21 ms | 78% | 20% | 2% |
| blake3 | 9.46 ms | 63% | 32% | 5% |

Both attributions are clean (marginals sum to ~100%, residual at or near
noise) — a substitution attribution is only valid when the hash-stubbed and
assembly-stubbed variants walk the *same loop shape* as the real path
(`gen_phase_variant` dispatches all three variants through one loop-shape
implementation per backend to guarantee this; a stub that takes a different
code path than the real one it stands in for produces a negative marginal
and a large residual — the signature of a broken attribution, not a real
result). blake3 spends proportionally less on hashing (63% vs 79%) and more
on assembly — expand/pack is the next shared optimization target, worth
~a fifth of either generator.

**Verify-shaped cost (2^k × m leaf recomputes + fold):**

| Config | blake2b | blake3 |
|---|---|---|
| (144,5) m=1 | 5.1 µs | 4.0 µs |
| (144,5) m=16 | 58.9 µs | 57.4 µs |
| (200,9) m=1 | 86.5 µs | 84.9 µs |
| (200,9) m=16 | 959 µs | 922 µs |

Verification is hash-setup-bound, not stream-bound, so the two hashes are
near-equal there. The (200,9) m=1 cost lands in the same ballpark as the
consensus-stack yardstick (zebro-bench's `equihash_verify`), a useful sanity
anchor. The m dial behaves as designed: cost scales mildly sublinearly in m
(iteration rounds are single-block), and (144,5) at m=16 — exactly the
`2^k·m = 512` budget bound — still verifies cheaper than (200,9) at m=1.

## 10. Round 3: `linux-arm64-vm` — first cross-platform regularized array

First measurement round on a second platform (2026-07-17), and the start of
the regularized-array discipline: same dimensions on every machine-of-record
— {implementation × backend × (n,k) × metric}, min/median/MAD per cell,
JSONL per machine. **Machine**: aarch64 Linux VM, 4 vCPU on a virtualized
Apple core (probe: `impl=0x61`), 3.9 GB RAM, rustc 1.97.1 / gcc (gnu17),
tag `linux-arm64-vm`. VM-grade caveat applies (virtualization jitter), but
repeatability was measured, not assumed: a second full round against the
first compared **24/24 records within the noise band** (0 wins, 0
regressions). Artifacts: `baselines/linux-arm64-vm.jsonl` (60 records, both
rounds), `SOLVER_CORPUS/rk/baselines/linux-arm64-vm.jsonl` (4 cases).

### 10.1 Req core array (Rust, release, defaults)

| Metric | (48,5) | (72,5) | (96,5) |
|---|---|---|---|
| leaf hash ns/leaf | 129.2 | 122.0 | 130.5 |
| solve-reference ms | 0.4 | 5.6 | 108.0 |
| solve-arena ms | 0.2 | 3.4 | 66.5 |
| solve-bucket ms | 0.2 | 3.2 | 58.9 |
| verify µs (reference/arena/early) | 5.6/4.7/4.1 | 5.8/–/– | 5.8/–/– |
| gen share of solve | 20.7% | 18.8% | 15.2% |

Shape matches the M4 record: merge-dominated, arena ≈ 1.6×, bucket best,
verifier flat.

### 10.2 C++ vs Rust, same machine, same params (extends §5)

| | C++ ref | Rust ref | C++ arena | Rust arena | C++ verify | Rust verify |
|---|---|---|---|---|---|---|
| (96,5) | 177.1 ms | 108.0 ms | 112.4 ms | 66.5 ms | 9.3 µs | 5.8 µs |

Rust ~1.6–1.7× faster across both solve backends and the verifier on this
platform/compiler pair (g++ -O2 vs rustc lto). First measured instance of
T4.5's cross-language comparison.

### 10.3 Memory array (`req_memcheck`, counting allocator)

| (n,k) | reference | ratio vs naive floor | arena | full-index model | measured/model |
|---|---|---|---|---|---|
| (40,4) | 135.6 KB | 30.1× | 67.5 KB | 99.8 KB | 1.36× |
| (48,5) | 136.8 KB | 27.4× | 82.9 KB | 145.5 KB | 0.94× |
| (72,5) | 1.87 MB | 18.4× | 2.07 MB | 2.30 MB | 0.81× |
| (96,5) | 55.1 MB | 27.6× | 66.0 MB | 36.75 MB | 1.50× |

The SIZING §1c full-index model holds on this second platform in the same
0.8–1.5× band it was calibrated to on macOS — cross-platform validation of
the model, and the (40,4) anchor replacing the invalidated (24,5) is now
measured (30.1×).

### 10.4 UniBlake: first Linux/gcc run (new platform validation)

`ub_test` **ALL GREEN** on aarch64 Linux/gcc — previously validated only on
arm64 macOS/clang; the NEON kernel passes the self-test gate here too.
Kernel array (`ub_kbench`, leaf shape prefix=140/out=50):

| kernel | ns/leaf | bulk MiB/s |
|---|---|---|
| ref | 124.2 | 1174 |
| ref_unrolled | **108.5** | **1435** |
| neon | 257.8 | 510 |

NEON = 0.43–0.48× (loses *harder* than bare-metal M4's 0.55–0.70×) —
another datapoint for T1.5's "NEON stays parked on big cores." Dispatch tax
0.23% median. Cross-check across languages: Rust's scalar leaf hash
(122–130 ns/leaf, §10.1) ≈ uniblake's C ref kernel (124.2) on identical
shape — Seam A parity across implementations, measured. **Portability
finding**: `ub_bench.c`/`ub_kbench.c` need `-std=gnu99` (or
`_POSIX_C_SOURCE`) on glibc for `clock_gettime`'s `timespec` — a BUILD.md
gap for the Linux directions; library sources compile clean under c99.

### 10.5 Corpus array (RK) and machine-limit exclusions

RK (`rk_bench`, 5 reps, two-instrument memory):

| case | wall median | allocator peak | OS RSS |
|---|---|---|---|
| n100_k4 | — | 548 MB | 810 MB (flagged: 32% disagree) |
| n108_k5 | 974 ms | 149 MB | 338 MB (flagged: 56% disagree) |

The allocator/RSS disagreement flags are the harness working as designed;
on a VM the RSS side includes virtualization overhead — treat allocator
numbers as primary here. Exclusions, with reasons: **RZ** — measured 6.27
GB peak (its own STATUS.md) exceeds this VM's 3.5 GB available; also its
`build.rs` resolves the vendored tromp C source via the local cargo
registry cache, absent on a fresh machine (environment-portability note
for the port). **CS C++** — needs cmake, not present in this VM. **cs-rs**
— 2/2 tests pass but no bench binary (the known T4.2 gap, reconfirmed).

### 10.6 PLAN corrections surfaced by this round

`rz_bench` is *already* reps+provenance+memory per its own module docs and
STATUS.md §6 numbers — T4.3's "fix rz_bench first" precondition is stale;
what actually blocks RZ re-baselining on new machines is memory (6.27 GB)
and the registry-cache dependency above.
