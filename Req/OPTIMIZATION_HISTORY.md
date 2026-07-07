# OPTIMIZATION_HISTORY.md — 2016-17 Equihash solver techniques, applied to Req

The 2016-17 Equihash optimization wave (the [Zcash Open Source Miner
Challenge](https://github.com/zcash/zcash/issues/1338)) produced four techniques
that took the reference solver from the paper's naive form to the memory floor.
This note records each from primary sources and maps it to this implementation —
what applies, what was applied, and the measured result. Findings context:
[../Equihash.md](../Equihash.md) F-A1, F-A5.

## The four canonical techniques

Sourced from [tromp/equihash](https://github.com/tromp/equihash),
[xenoncat/equihash-xenon](https://github.com/xenoncat/equihash-xenon), the
[Openwall analysis](https://www.openwall.com/articles/Zcash-Equihash-Analysis),
zcash `src/crypto/equihash.cpp` (`BasicSolve`/`OptimisedSolve`), and the 2025
regularity paper's Proposition 3-4 account.

| # | Technique | Mechanism | Origin |
|---|---|---|---|
| 1 | Compact index-pointer storage | Store a binary tree of index *pairs*, not growing index lists; reconstruct full indices only at solution time. Cuts index space by a factor of (2^k)/k. | xenoncat, tromp |
| 2 | Incomplete bucket sort | Bucket rows by the collision digit (counting sort, O(m)) instead of a full comparison sort (O(m log m)); never materialize a fully sorted list. | tromp, xenoncat |
| 3 | Static allocation | Allocate all working memory once from the parameters; no per-round growth. tromp: "I borrowed from xenoncat the idea to allocate all memory statically." | xenoncat -> tromp |
| 4 | In-place merge | Write merged rows back into freed slots of the sorted input (`posFree` in zcash BasicSolve), never holding two full copies. | zcash reference |

Concrete memory figures from the record: xenoncat's solver used 178 MB at
(200,9); tromp reduced buckets from 2^16 to 2^12 and improved layout to reach
144 MB (a 7% layout gain plus the bucket reduction). These are the numbers behind
Equihash.md's "roughly 49 MB peak" once index pointers are counted — the solvers
drove memory to the floor the single-list algorithm permits, which is why the
2017 optimization wave plateaued (F-A5).

## Mapping to this implementation

The Requihash context changes which techniques are safe. In Equihash, techniques
1-2 are what collapsed ASIC resistance (they fix the memory-access pattern an ASIC
wants). In **Requihash they are safe to use**, because the regularity constraint
blocks the single-list algorithm those techniques accelerate — the k-list solver
they run on costs an ASIC >=2x memory rather than less (F-A4). So we can adopt the
performance wins without reopening the vulnerability.

| Technique | Prior state | Applied? | Result |
|---|---|---|---|
| 2. Incomplete bucket sort | full `sort_by` (24% of solve, BENCHMARK.md) | **Yes** — `solve::bucket::BucketSolver` | (96,5) 93.0 -> 80.0 ms, **14% faster** than arena; 1.86x cumulative vs reference |
| 3. Static allocation | per-round `Vec` growth | **Partial** — arena preallocates the leaf buffer; bucket counting-sort arrays sized from param | folded into the arena/bucket wins |
| 4. In-place merge | fresh out-buffers each round | **No** (deliberate) | the arena's flat SoA already avoids per-row alloc; in-place would save the round's out-buffer alloc but complicates the bucket scatter — deferred |
| 1. Compact index-pointer storage | full explicit index vectors per row | **No** (tracked) | our verifier + wire format consume full index vectors; pointer storage stores pairs and reconstructs at the end — a larger change with a real (2^k)/k space payoff, the clear next memory optimization |

## Measured progression at (96,5)

| Stage | Solver | ms | Cumulative speedup |
|---|---|---|---|
| Round 0 | reference (naive, per-row Vec) | 149.1 | 1.00x |
| Round 1 | arena (flat SoA, kills 59% alloc) | 93.0 | 1.60x |
| Round 2 | bucket (2016-17 incomplete sort, kills sort cost) | 80.0 | **1.86x** |

The bucket solver single-threaded (80.0 ms) also beats the rayon-parallel
generation solver (81.2 ms), because the sort was a larger cost than the
generation phase parallelism recovered — the profile said merge dominates, and
attacking the merge's sort beat parallelizing the generation.

All three solvers produce byte-identical solution sets (`all_solvers_agree`
test), so every optimization is correctness-preserving.

## Next, in priority order

1. **Compact index-pointer storage (technique 1).** The one canonical 2016-17
   technique not yet applied, and the one with a proven (2^k)/k space win. Requires
   the merge to store index *pairs* (parent pointers) and a final reconstruction
   pass to expand to full index vectors for the verifier/wire format. Biggest
   remaining memory lever, and what makes production (200,9) mining feasible.
2. **In-place bucket merge (technique 4).** Fold the round output back into the
   working buffer to drop the per-round out-buffer allocation.
3. **Bucket-parallel merge (Tier 2).** Now that buckets are explicit, each bucket
   is an independent unit of work — the natural parallel decomposition rayon's
   generation-only solver could not reach.
