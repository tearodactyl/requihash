# Equihash.md — Topic A Findings: Equihash and Memory-Hard Proof-of-Work

Prerequisites: the proof-of-work and memory-hardness cluster of the [FableFrontiers.md](FableFrontiers.md) Section 3 terminology primer. This document captures the Equihash record in full — design origins, the 2016–2017 solver optimization wave, the 2018–2019 ASIC defeat, the generalized-birthday theory that was reworked in 2025, and the Requihash repair — following the template of FableFrontiers.md Section 5 with evidence grades per FableFrontiers.md Section 4. The primary source for the 2025 material is Tang–Sun–Gong, "[On the Regularity of the Generalized Birthday Problem](https://eprint.iacr.org/2025/1351)," read in full.

## Table of Contents

1. [Scope and Stakes](#1-scope-and-stakes)
2. [State of the Art](#2-state-of-the-art)
3. [Analysis](#3-analysis)
4. [The 2025 Theory Foundations and Their Relationship to This Work](#4-the-2025-theory-foundations-and-their-relationship-to-this-work)
5. [Voices and Dissent](#5-voices-and-dissent)
6. [Findings](#6-findings)
7. [Conclusions](#7-conclusions)
8. [Further Directions](#8-further-directions)
9. [References](#9-references)

## 1. Scope and Stakes

Equihash was the flagship of the memory-hard proof-of-work generation: a puzzle whose solver must hold a large working set, on the theory that commodity DRAM is a more egalitarian resource than custom logic, with instant verification as the asymmetry that makes it consensus-grade. Its ten-year record is the best single case study of how ASIC resistance actually fails — not through silicon exotica but through algorithm and data-structure work — and its 2025 theoretical rework is the sharpest available lesson in what happens when a deployed cryptographic design rests on a mis-specified problem. The stakes are not historical: Equihash-secured chains still carried over $4 billion in aggregate market capitalization as of February 2026 [Reported: eprint 2025/1351], and the repair now on the table is a client-side-only change any of them could adopt.

The document answers the two questions the FableFrontiers.md Section 9.1 brief posed — why the 2016–2017 optimization wave plateaued, and why ASIC resistance failed — and captures the answer the field could not have given until 2025: both questions have the same answer, and it lives at the algorithm-data-structure boundary rather than in hardware. Beyond the survey, this program built a working Requihash implementation (`Req/`, C++ and Rust, cross-validated), so the Analysis and Findings below carry Measured evidence — reproduced wire sizes, a byte-exact regularity constraint, and a solver profile that re-derives the 2016-17 techniques — not only Reported claims.

## 2. State of the Art

The timeline runs from design through defeat to rework; the pattern to notice is that every inflection is an algorithms result, with hardware only ever cashing in what the algorithms made possible.

| Date | Event | Significance |
|---|---|---|
| 2002 | [Wagner's generalized birthday algorithm](https://people.eecs.berkeley.edu/~daw/papers/genbday.html) | The k-tree algorithm: sub-birthday-bound solving of the k-list XOR problem, the complexity anchor for everything below |
| 2016 | Biryukov–Khovratovich publish [Equihash](https://eprint.iacr.org/2015/946) at NDSS ([journal version](https://ledger.pitt.edu/ojs/ledger/article/view/48), Ledger 2017; [reference solver](https://github.com/khovratovich/equihash)) | GBP-based PoW with algorithm binding against amortization; adopted by Zcash at (n,k) = (200,9) per [protocol spec section 7.7.1](https://zips.z.cash/protocol/protocol.pdf) |
| Oct 2016 | [Zcash Open Source Miner Challenge](https://github.com/zcash/zcash/issues/1338) | Kicks off the optimization wave: [tromp's solvers](https://github.com/tromp/equihash), [xenoncat's AVX2 solver](https://github.com/xenoncat/equihash-xenon), [SILENTARMY](https://github.com/mbevand/silentarmy) GPU solver, [nheqminer](https://github.com/nicehash/nheqminer) |
| 2016–17 | The index-pointer technique emerges in [xenoncat's solver](https://github.com/xenoncat/equihash-xenon) | Stores index pointers instead of expanding XOR values; collapses peak memory and fixes access patterns — the single most consequential optimization in the record |
| 2017 | [Alcock–Ren note](https://dl.acm.org/doi/10.1145/3140649.3140652) at CCSW; [Openwall analysis](https://www.openwall.com/articles/Zcash-Equihash-Analysis) | First public flags that Equihash's problem does not match Wagner's, and that no tradeoff-resistance bound is proven for it |
| Aug 2017 | [Dinur–Nadler TMTO attack on MTP](https://eprint.iacr.org/2017/497), CRYPTO | Sub-linear computation-memory tradeoff breaks the sibling scheme's steepness claim — the cautionary TMTO result for the whole family |
| May 2018 | Bitmain ships the Antminer Z9 against (200,9) | ASIC resistance publicly falls; [Bitcoin Gold forks](https://github.com/BTCGPU/BTCGPU) to Zhash (144,5) with [multi-parameter solvers](https://github.com/BTCGPU/equihash); Zcash never changes parameters |
| 2019 | [Bai–Gao–Hu–Zhang adversary solver](https://www.ndss-symposium.org/ndss-paper/constructing-an-adversary-solver-for-equihash/), NDSS | The off-chip technique: a multi-chip ASIC design built on index pointers, significantly outperforming CPU/GPU mining — the defeat, formalized |
| 2024 | Esser–Santini regular syndrome decoding (Crypto), Dinur–Keller–Klein fine-grained k-SUM bounds (JACM) | The regularity toolkit and conjecture landscape the 2025 rework builds on |
| 2025 | Ducas–Engelberts–Loyer (Crypto 2025): Wagner runs subexponentially for SIS-infinity | The GBP solver family becomes a practical-security instrument against ML-DSA/Dilithium — recorded in [PostQuantum.md](PostQuantum.md) F-B10 |
| 2025 | [Blocki-Smearsoll, provably memory-hard PoW](https://eprint.iacr.org/2025/1456) | Generic pebbling-based lower bounds on prover memory; formally rehabilitates the [MTP framework](https://arxiv.org/abs/1606.03588) wounded by [2017 trade-off attacks](https://eprint.iacr.org/2017/497) |
| 2025 | Tang–Sun–Gong, "[On the Regularity of the Generalized Birthday Problem](https://eprint.iacr.org/2025/1351)" ([artifacts](https://github.com/tl2cents/Generalized-Birthday-Problem)) | The rework: regular/loose GBP distinction, corrected complexity accounting, the self-merge reduction, parameter bounds, and the Requihash repair |

The competing memory-hard designs form the comparison set the brief asked for, and the 2025 lens sorts them more cleanly than the original capacity/bandwidth taxonomy did [Reported for statuses; the final column is this document's assessment].

| Scheme | Hardness style | Deployed by | Status under the 2025 lens |
|---|---|---|---|
| [Equihash](https://eprint.iacr.org/2015/946) | Capacity via sorting/merging | Zcash, Bitcoin Gold, others | ASIC-defeated at (200,9) via index pointers plus off-chip memory; repairable via Requihash |
| [MTP](https://arxiv.org/abs/1606.03588) | Capacity via Merkle-proved Argon2 | Zcoin, historically | Attacked 2017; formally rehabilitated by the Blocki-Smearsoll pebbling framework (eprint 2025/1456) |
| [Cuckoo Cycle](https://github.com/tromp/cuckoo) | Capacity via graph cycles | [Grin](https://github.com/mimblewimble/grin) | Alive; ASIC-friendly variants deliberately embraced instead of resisted |
| [RandomX](https://github.com/tevador/RandomX) | CPU-shaped random programs | Monero | The strongest surviving ASIC-resistance claim, by targeting general-purpose execution rather than memory |
| [Autolykos](https://github.com/ergoplatform/Autolykos) | Bandwidth | Ergo | Alive; bandwidth hardness caps rather than eliminates ASIC advantage per [Ren–Devadas theory](https://eprint.iacr.org/2017/225) |
| [kHeavyHash](https://github.com/kaspanet/rusty-kaspa) | Small matrix multiply in a hash sandwich | Kaspa | Mined on ASICs; the bridge to the tensor-puzzle generation covered in [InferencePoW.md](InferencePoW.md) |
| [Argon2](https://www.rfc-editor.org/rfc/rfc9106) | Capacity, password-hashing lineage | Not a PoW; the memory-hard function standard | RFC 9106; the Biryukov–Khovratovich design that outlived their PoW |

## 3. Analysis

**The mis-specification.** Wagner's algorithm solves the *regular* GBP — k separate lists, one element from each (RGBP in the 2025 paper's terms). Equihash instead has the solver draw all 2^k elements from a *single* list (the loose variant, LGBP), bound to a tree structure by its algorithm-binding constraint. The two problems were treated as interchangeable for two decades, including during Equihash's design; [Alcock and Ren flagged the mismatch in 2017](https://dl.acm.org/doi/10.1145/3140649.3140652) and the [2025 rework](https://eprint.iacr.org/2025/1351) finally quantified it: the regular and loose variants differ by a √2 factor in the complexity exponent, and the single-list algorithm (introduced by Levieil–Fouque for LPN in 2006) is not merely an optimization of Wagner's but a different algorithm for a different problem. Two concrete corollaries fall out. First, a parameter-validity bound: k must not exceed √(n/2+1), which invalidates parameter sets like (192,11) that treat k as a free dial — Zcash's (200,9) and Bitcoin Gold's (144,5) sit inside the bound [Reported]. Second, the paper's broader complexity program: an information-set-decoding framework (via reduction to syndrome decoding) that beats the 2^(n/2) worst-case birthday bound whenever k/n exceeds 0.188 in the regular case or 0.11 in the loose case, disproving the average-case-to-worst-case k-XOR conjecture for non-constant k, with a collision attack on the iSHAKE256 incremental hash reducing its security bound from 2^256 to 2^189 as the headline application [Reported].

**Where the memory hardness actually lives.** The 2025 paper reduces Equihash's memory-capacity hardness to the *self-merge problem*: given a single list of size N = 2^(ℓ+1), find *all* pairs colliding on ℓ bits. Single collisions fall to constant-memory rho/cycle-detection methods in O(2^(ℓ/2)) time, but no memory-efficient linear-time algorithm for *all* collisions is known — that residue is the hardness Equihash retains, and it is why the paper judges that Equihash still resists single-chip ASICs and provides "considerable" capacity and bandwidth hardness even in its weakened state [Reported].

**The time-memory tradeoff, and why 2017 is the pivotal year for it.** A memory-hard PoW's real security is not the memory it needs but its *tradeoff steepness*: reduce memory by a factor q and computation rises by roughly q^s, where higher s means a memory-cutting ASIC pays more. The [Equihash paper](https://ledger.pitt.edu/ojs/ledger/article/view/48) claims steepness through two propositions — plain Wagner has steepness (k−1)/2 (its Proposition 4, penalty C₁(q) ≈ (3q^((k−1)/2)+k)/(k+1)), while *algorithm binding* raises it to k/2 (Proposition 6, C₂(q) ≈ 2^k·q^(k/2)·k^(k/2−1)); the out-of-memory Bernstein tradeoff is penalty C(q)=q^k naively, improved to ≈4·2^(n/(k+1))·q^((k+1)/2) with a memoryless last step. At (200,9) the k/2 = 4.5 steepness underwrites the paper's headline that a 250 MB-limited adversary pays ~1000x and a memoryless one ~2^75 hash calls. Three 2017 results define what this claim is worth. First, [Alcock and Ren](https://dl.acm.org/doi/10.1145/3140649.3140652) showed the steepness propositions analyze the *regular* k-list problem while Equihash solves the *loose single-list* one, so no tradeoff-resistance bound is actually proven for the problem Equihash uses — the TMTO face of the mis-specification (F-A2). Second, [Dinur and Nadler](https://eprint.iacr.org/2017/497) (CRYPTO 2017) broke the sibling MTP scheme with a sub-linear computation-memory tradeoff, the concrete demonstration that a memory-hard PoW's steepness claim can fall to an unforeseen attack. Third, the [2017 MTP trade-off attacks](https://eprint.iacr.org/2017/497) motivated the pebbling-lower-bound program that only closed in 2025 (F-A6). The through-line: until 2025 no steepness claim in this family was a theorem, and Equihash's was a claim about the wrong problem — the analysis in [Req/SECURITY_ANALYSIS.md](Req/SECURITY_ANALYSIS.md) §8-8a works out how the working implementation can now measure the real steepness of the regular problem Requihash restores [Reported].

**How the ASIC resistance fell.** The mechanism is a two-step data-structure story, not a silicon story. Step one, 2016–17: the index-pointer technique from [xenoncat's challenge-winning solver](https://github.com/xenoncat/equihash-xenon) stores pointers into earlier layers instead of materializing XOR values, which both collapses peak memory and — the ASIC-relevant part — fixes the memory-access pattern and the per-merge memory bound, exactly the regularity an ASIC memory controller wants. The paper's Proposition 4: the single-list algorithm with index pointers runs in O(kN) time with peak memory around O(n·N) bits, roughly 49 MB at Zcash's (200,9) [Reported]. Step two, 2019: [Bai et al.](https://www.ndss-symposium.org/ndss-paper/constructing-an-adversary-solver-for-equihash/) built the off-chip multi-chip ASIC solver on that fixed access pattern, significantly outperforming commodity hardware — which is what Bitmain had been shipping commercially since the 2018 Z9 generation. Subsequent solver work (post-retrieval and in-place merge techniques) cut peak memory by a further factor above two [Reported: cited as recent in eprint 2025/1351]. This sequence answers the brief's plateau question: the 2017 optimization wave stopped because the index-pointer representation had already reached the memory floor the single-list algorithm permits — there was nothing left for GPU-feature engineering to harvest, and the planned "modernized solver kernel" prototype of FableFrontiers.md Section 9.1 would have measured a floor, not headroom.

**The 2016-17 solver techniques, concretely.** The [Zcash Open Source Miner Challenge](https://github.com/zcash/zcash/issues/1338) produced four techniques that together drove the reference solver from the paper's naive form to the memory floor; they are worth enumerating because each maps to a distinct cost centre, and because the same techniques are what a modern implementation re-derives [Reported: [tromp](https://github.com/tromp/equihash) and [xenoncat](https://github.com/xenoncat/equihash-xenon) solver sources, [Openwall analysis](https://www.openwall.com/articles/Zcash-Equihash-Analysis), zcash `src/crypto/equihash.cpp` `BasicSolve`/`OptimisedSolve`]. First, *compact index-pointer storage*: store a binary tree of index *pairs* rather than growing lists of full index tuples, reconstructing complete indices only at solution time — a space saving of a factor of (2^k)/k, the single largest memory win and the technique the 2025 paper singles out as decisive. Second, *incomplete bucket sort*: identify collisions by bucketing rows on the collision digit (a counting-sort partition, linear time) rather than the full comparison sort Wagner's algorithm is usually written with, and never materialize a fully sorted list. Third, *static allocation*: size all working memory once from the parameters (xenoncat's idea, which tromp adopted and improved). Fourth, *in-place merge*: write merged rows back into the freed slots of the sorted input array (the `posFree` cursor in zcash `BasicSolve`), so the solver never holds two full copies of the working set. The concrete memory record: xenoncat's solver used 178 MB at (200,9); tromp reduced the bucket count from 2^16 to 2^12 and improved the layout to reach 144 MB (a 7% layout gain on top of the bucket reduction), and multi-threading was, in tromp's words, "crucial for 144,5." These are the engineering steps behind the "roughly 49 MB peak once index pointers are counted" figure — the solvers drove memory to the floor the single-list algorithm permits [Reported].

**The repair.** Requihash — Section 5.2 of the 2025 paper — is the minimal modification that re-anchors Equihash to the regular problem: add the sequential constraint that the i-th solution element must come from position class i-1 mod K, so the solution must span K implicit lists. That single constraint makes the single-list algorithm — and with it the index-pointer optimization — structurally inapplicable; against the surviving k-list algorithm, index pointers *at least double* memory rather than reduce it (a 12x memory penalty for an ASIC attempting the technique at (200,2^9)), and the paper's Proposition 3 gives the k-list-with-index-trimming costs: O(2^k·N) time with peak memory ((k²+5k+2)/4·ℓ+2^(k-1))·N bits [Reported]. The concrete trade at Zcash-class parameters, from the paper's Table 3: time 2^24.2 to 2^30.6 (a factor of 84.4, absorbed by difficulty retuning), peak memory 2^28.6 to 2^30.8 bits — 49 MB to 223 MB, a factor of 4.6 — and wire solution size *drops* from 1344 to 1280 bytes because the index field can be reconstructed from a predefined packet structure. For every parameter set with k of at least 5, peak memory at least doubles; larger k amplifies the advantage, and Requihash frees k from the √(n/2+1) bound. The change is client-side only with negligible verification-cost increase, is amortization-free under algorithm binding, and canonical ordering eliminates the duplicate-solution nuisance [Reported]. The full parameter table:

| (n, K) | Equihash time | Equihash memory | Equihash solution | Requihash time | Requihash memory | Requihash solution |
|---|---|---|---|---|---|---|
| (96, 2^5) | 2^19.3 | 2^23.6 | 68 B | 2^22.6 | 2^24.8 | 64 B |
| (128, 2^7) | 2^19.8 | 2^24.0 | 272 B | 2^24.6 | 2^25.7 | 256 B |
| (160, 2^9) | 2^20.2 | 2^24.3 | 1088 B | 2^26.6 | 2^26.6 | 1024 B |
| (144, 2^5) | 2^27.3 | 2^32.2 | 100 B | 2^30.6 | 2^33.4 | 96 B |
| (150, 2^5) | 2^28.3 | 2^33.2 | 104 B | 2^31.6 | 2^34.4 | 100 B |
| (200, 2^9) | 2^24.2 | 2^28.6 | 1344 B | 2^30.6 | 2^30.8 | 1280 B |
| (288, 2^8) | 2^36.0 | 2^41.2 | 1056 B | 2^41.6 | 2^42.9 | 1024 B |

**A working Requihash implementation, and what building it measured.** This program produced a reference implementation of Requihash — miner and verifier in both C++ (zcash `src/crypto/equihash` conventions) and Rust (zebra verifier conventions), wire-compatible across the two — in `Req/` alongside this document. It confirms the paper's claims at the artifact level and adds Measured evidence the paper could not. Three results stand out. The Table 3 wire sizes reproduce exactly: at (200,9), Equihash-compatible encoding is 1344 bytes and the Requihash compact encoding is 1280 bytes, the 64-byte reduction the regularity constraint permits [Measured]. The regularity is byte-exact and mechanical: leaf i is keyed by (i mod k, i / k), so removing the "i mod k" term recovers single-list Equihash — the minimal client-side change made concrete, cross-validated so a C++-mined solution verifies in the independent Rust verifier [Measured]. And profiling the solver localizes cost in a way that vindicates the 2016-17 techniques from first principles: at small parameters the naive solver's time is dominated by per-row heap allocation (59% of samples) and comparison sorting (24%), not by BLAKE2b hashing (17%); replacing the growing-list layout with a flat arena gave a 1.6x speedup, and replacing the comparison sort with the tromp/xenoncat incomplete-bucket-sort gave a further 14%, for 1.86x cumulative — the same two techniques the 2016-17 wave found, re-derived by measurement [Measured; harness and per-backend numbers in `Req/BENCHMARK.md` and `Req/ARCHITECTURE.md` §7]. The one canonical 2016-17 technique not yet ported, compact index-pointer storage with its (2^k)/k space win, is the identified next step and the change that makes production (200,9) mining feasible.

**The proven-hardness turn.** In parallel, [Blocki and Smearsoll (eprint 2025/1456)](https://eprint.iacr.org/2025/1456) replace solver-conjecture security with theorems: a generic lower bound on any prover's cumulative memory cost via the pebbling cost of the ex-post-facto graph, formally proving the soundness of the [MTP framework](https://arxiv.org/abs/1606.03588) — the Biryukov–Khovratovich design that [2017 trade-off attacks](https://eprint.iacr.org/2017/497) had wounded — when instantiated with suitable data-independent memory-hard functions. Together with the regularity rework, this marks the design paradigm shift: first-generation memory-hard PoW was secure *if* the best-known solver was optimal (it was not); the second generation is secure by lower bound [Reported].

**The quantum margin.** Quantum k-XOR algorithms (Grassi–Naya-Plasencia–Schrottenloher line) solve the GBP in O(2^(n/(2+log₂k))) against Wagner's classical O(2^(n/(1+log₂k))) — a bounded speedup requiring large quantum-accessible memory, the least credible near-term quantum resource. Combined with F-B7's Grover analysis, memory-hard PoW inherits the same graceful quantum degradation as hash PoW [Reported, textbook-level; no link of adequate stability located].

## 4. The 2025 Theory Foundations and Their Relationship to This Work

Three 2025 results form the theoretical backdrop against which this document's survey and the `Req/` implementation both sit: the regularity rework (Tang–Sun–Gong, [eprint 2025/1351](https://eprint.iacr.org/2025/1351)), already covered in Section 3, plus two that this section relates explicitly to the work — "Wagner's Algorithm Provably Runs in Subexponential Time for SIS^∞" (Ducas–Engelberts–Loyer, [eprint 2025/575](https://eprint.iacr.org/2025/575)) and "Provably Memory-Hard Proofs of Work with Memory-Easy Verification" (Blocki–Smearsoll, [eprint 2025/1456](https://eprint.iacr.org/2025/1456)). The organizing observation is that 2025 was the year memory-hard-PoW security moved from *conjecture* to *theorem* along two independent axes — the solver's power and the prover's memory floor — and the Requihash work and this implementation sit precisely at the intersection.

### 4.1 The two papers, precisely

The two theory papers attack opposite ends of the same security question and neither is about Equihash directly, which is exactly why their relationship to this work needs stating.

| | Wagner Provably Runs (2025/575) | Provably Memory-Hard PoW (2025/1456) |
|---|---|---|
| Question | How *powerful* is the solver (Wagner's algorithm)? | How *high* is the prover's forced memory floor? |
| Object | SIS^∞ (lattice short-integer-solution) | MTP framework on data-independent MHFs |
| Result | Wagner solves SIS^∞ in proven subexponential exp(O(n/log log n)) time — first *proven*, not heuristic (prior: Kirchner–Fouque CRYPTO 2015 claim) | Malicious prover's cumulative memory cost is ≥ Ω(N²/log N) when the iMHF graph is depth-robust (DRSample); honest prover O(N) time, proof and verification polylog(N) |
| Method | Re-reads the Wagner step as walking backward through projected lattices with Gaussian randomized bucketing | Lower-bounds cumulative memory by the pebbling cost of the ex-post-facto graph |
| Stated limit | Does *not* threaten Dilithium's concrete security | Applies to the MTP/iMHF family, not to Equihash's GBP structure |

### 4.2 Theoretical relationship — the two-sided closure

The two papers, with the regularity rework, close a gap that stood open since 2016: no first-generation memory-hard PoW had a *proven* security guarantee, only "secure if the best-known solver is optimal." They close it from both sides.

*Solver side (upper bound on hardness).* The Wagner-provably-runs paper is the first rigorous characterization of how far Wagner's algorithm reaches — the same algorithm whose Equihash behavior Section 3 analyzes. It matters to this work as a *proof-technique* import, not a threat: it demonstrates that Wagner's runtime, long treated heuristically (including in Equihash's own propositions), can be made a theorem, and it establishes the CWI group's projected-lattice/Gaussian-rounding machinery as the modern way to prove such bounds. F-A7 records the lattice-facing consequence; the Topic-A-facing consequence is that the tradeoff-steepness claims this document flagged as unproven (F-A10) are now provable in principle by the same style of argument.

*Prover side (lower bound on hardness).* The Blocki–Smearsoll paper supplies the missing lower bound: a prover *cannot* avoid Ω(N²/log N) cumulative memory cost, a theorem about the honest floor rather than a conjecture about the best attack. It rehabilitates MTP — wounded by the 2017 Dinur–Nadler tradeoff (F-A10) — by proving the memory-hardness the 2017 attack had cast into doubt, and it is the "second generation is secure by lower bound" claim of F-A6 made concrete with a specific bound and construction.

Together they define the theoretical frame Requihash lives in: the regularity rework says Equihash solved the wrong problem, the Wagner paper shows the solver's power over the right (regular) problem is now rigorously analyzable, and the memory-hard paper shows a prover memory floor can be *proven* for a memory-hard PoW at all. Requihash is the object that needs all three: it restores the regular problem (rework), its steepness is a claim about Wagner's power over that problem (Wagner paper's style), and its 12x-penalty security is ultimately a prover-memory-floor claim (memory-hard paper's style).

### 4.3 Practical relationship — what it means for the implementation

For the `Req/` implementation the two papers translate into a concrete division between what is settled and what the code should measure.

*What the theory settles (so the implementation need not re-litigate).* The MTP-style memory floor is now a theorem for depth-robust iMHFs, so a memory-hard PoW *can* have a proven floor — the implementation's job is not to prove hardness from scratch but to sit at the honest operating point and expose the tradeoff surface. The Wagner-provably-runs result confirms the solver family is a well-characterized object, so the implementation's solvers (`solve::reference/arena/bucket`) are instances of an algorithm whose asymptotic behavior is now understood, not a black box.

*What the theory leaves for measurement (the implementation's actual contribution).* Neither 2025 theory paper is about Equihash's or Requihash's *concrete* constants — the Wagner paper is asymptotic exp(O(n/log log n)) and disclaims concrete-security impact; the memory-hard paper is about MTP/iMHFs, not the GBP tree. So the empirical steepness of the regular Requihash problem, the concrete (200,9) memory floor, and whether the 12x penalty holds against a real memory-reduced solver are all *unmeasured*, and the `Req/` harness is where they get measured (the TMTO experiments of [Req/SECURITY_ANALYSIS.md](Req/SECURITY_ANALYSIS.md) §8-8a, the index-pointer memory work of [Req/ARCHITECTURE.md](Req/ARCHITECTURE.md) §7). The theory says a proof is possible and characterizes the solver; the implementation supplies the numbers the proofs are asymptotic about.

### 4.4 Toward defining and optimizing the algorithms — the theory-practice map

The full landscape of defining and optimizing Equihash- and Requihash-related algorithms sorts into four quadrants along two axes: theoretical vs. practical, and general (the GBP/memory-hard-PoW class) vs. specific (Requihash as this document defines it). The map is the organizing deliverable of this section.

| | General (GBP / memory-hard PoW class) | Specific (Requihash as defined here) |
|---|---|---|
| **Theoretical** | Regular-vs-loose GBP complexity (√2 exponent gap, 2025/1351); Wagner's proven subexponential reach (2025/575); provable prover memory floor via pebbling (2025/1456); TMTO steepness as the security metric (2017); open problems: self-merge lower bound, sub-2^(n/2) k-SUM, tight k=o(n) bound | Requihash restores the regular k-list problem, so the k/2 steepness argument applies legitimately (F-A10); the 12x ASIC memory penalty and Table 3 costs are the specific instantiation; parameter validity k ≤ √(n/2+1) is respected by construction |
| **Practical** | The 2016-17 solver techniques (index pointers, incomplete bucket sort, static allocation, in-place merge) as the general optimization toolkit; the memory floor those techniques reach; RandomX/Cuckoo/Autolykos as alternative anchor strategies | The `Req/` implementation: cross-validated C++/Rust miner+verifier, multi-backend seam structure, arena+bucket solvers at 1.86x, reproduced Table 3 wire sizes, and the unbuilt next steps — compact index-pointer storage for (200,9) feasibility, and the TMTO steepness measurements |

Read across the top row: the theory now characterizes the solver's power and the prover's floor for the *general* class, and Requihash inherits a *legitimate* version of the steepness claim Equihash could only assert. Read across the bottom row: the general optimization toolkit is 2016-17-complete and re-derived in the implementation, and the specific Requihash work is where the general theory's asymptotic guarantees get turned into measured constants. The diagonal is the point of the whole program: **theory (top) says what is provable and Requihash makes the claims legitimate; practice (bottom) supplies the constants the theory is silent about, on the exact scheme the theory made sound.**

## 5. Voices and Dissent

The Equihash record has fewer named public disputes than the post-quantum debate, but the individual voices that exist called the outcome early, and the pattern of who was right is worth recording. Three voices follow: the early critics, the solver authors, and the reworkers.

### 5.1 Early Critics

The mis-specification was flagged within a year of deployment, by two independent parties. Leo Alcock and Ling Ren's [CCSW 2017 note](https://dl.acm.org/doi/10.1145/3140649.3140652) was the first to state that Equihash's single-list problem is not the problem Wagner's algorithm solves and that the security argument therefore had a hole — the same Ling Ren whose [bandwidth-hard functions work](https://eprint.iacr.org/2017/225) with Devadas established that memory-hardness *caps* rather than eliminates ASIC advantage, the theory the whole generation's economics obeyed. Solar Designer's commissioned [Openwall analysis](https://www.openwall.com/articles/Zcash-Equihash-Analysis) of Zcash's Equihash use raised the practitioner-side versions before mainnet mattered: sensitivity of the security claims to solver algorithmics, and parameter choices whose margins were asserted rather than demonstrated. Neither critique changed deployment — Zcash kept (200,9) through the Z9, the Z11, and beyond, while Bitcoin Gold's post-ASIC fork to (144,5) was reactive. The eight-year gap between the 2017 flags and the 2025 formalization is the record's clearest governance lesson [Reported].

### 5.2 Solver Authors

The optimization wave was carried by individuals, mostly outside institutions. John Tromp — also Cuckoo Cycle's designer — wrote the [reference-grade open solvers](https://github.com/tromp/equihash) and has been the field's most consistent practitioner voice on solver complexity claims; the pseudonymous xenoncat produced the [AVX2 solver](https://github.com/xenoncat/equihash-xenon) whose index-pointer representation won the [miner challenge](https://github.com/zcash/zcash/issues/1338) and, in the 2025 paper's assessment, did more to collapse Equihash's ASIC resistance than any hardware advance; Marc Bevand's [SILENTARMY](https://github.com/mbevand/silentarmy) opened the GPU path. The irony the record preserves: the open-source challenge Zcash ran to democratize mining produced the exact data structure that later powered the [NDSS 2019 adversary ASIC](https://www.ndss-symposium.org/ndss-paper/constructing-an-adversary-solver-for-equihash/). Optimization prizes surface the attack surface — a dual-use lesson that generalizes well beyond Equihash [Reported plus interpretive]. [SOLVERS.md](SOLVERS.md) §0 goes deeper on both authors from their own primary sources — Khovratovich's original CC0 reference solver (settable N/K, predates the whole optimization wave), xenoncat's own 5-page algorithm-description PDF (the exact binary-tree/bucket/pairs-compression design behind the index-pointer technique), and tromp's own README narrating his relationship to xenoncat's work with real author-stated memory figures for both (144MB vs. 178MB at (200,9), not the paper's 49MB asymptotic estimate) [Measured, self-reported by the implementers].

### 5.3 Reworkers

Lili Tang, Yao Sun, and Xiaorui Gong (Chinese Academy of Sciences) supply the record's theoretical closure in [eprint 2025/1351](https://eprint.iacr.org/2025/1351), with runnable [artifacts](https://github.com/tl2cents/Generalized-Birthday-Problem) — and their framing is pointedly corrective of the field, not just of Equihash: the regular/loose conflation "biased the design and security analysis of GBP-based schemes" for two decades, and their four listed open problems (tightness of the k = o(n) bound, lattice algorithms for k-XOR at density 1, sub-2^(n/2) k-SUM for non-constant k, and single-list optimization in the k window between √(n/2+1) and √(n+1)) define the field's current theory frontier. The original designers are absent from this chapter by trajectory rather than dispute — Khovratovich building Ethereum's hash-based signatures, Biryukov running post-quantum cryptanalysis, both covered in [PostQuantum.md](PostQuantum.md) Section 4.5 — so the 2016 design's repair is being specified entirely by successors [Reported].

## 6. Findings

Findings are numbered for citation, most consequential first.

**F-A1.** Equihash's ASIC resistance was defeated at the algorithm-data-structure boundary, not by superior silicon: the index-pointer representation (2016–17) fixed access patterns and collapsed peak memory to roughly 49 MB at Zcash's (200,9), and the off-chip multi-chip ASIC (NDSS 2019) industrialized that structure. Hardware only cashed in what the data structure conceded — the general lesson recorded as F-X7 in [HardwareBridge.md](HardwareBridge.md) [Reported].

**F-A2.** The design rested on a mis-specified problem from birth: Equihash binds a *loose single-list* GBP while its security argument imported *regular k-list* complexity, a distinction worth a √2 factor in the exponent, flagged by Alcock–Ren in 2017 and formalized only in 2025. Parameter validity requires k at most √(n/2+1); sets like (192,11) are malformed, while (200,9) and (144,5) are inside the bound [Reported].

**F-A3.** What survives is the self-merge problem: finding all ℓ-bit collisions in a single list has no known memory-efficient linear-time algorithm, so Equihash retains meaningful capacity and bandwidth hardness and still resists single-chip ASIC implementation even in its weakened state [Reported].

**F-A4.** Requihash is a deployable repair, not a redesign: one sequential constraint restores regularity, disables the single-list algorithm and its index-pointer optimization (which then costs double rather than less, a 12x ASIC memory penalty at (200,2^9)), at least doubles peak memory for all k of 5 or more (49 MB to 223 MB at Zcash parameters), shrinks wire solutions, is amortization-free, and touches only client-side logic. The 84x time increase is absorbed by difficulty retuning. For the multi-billion-dollar Equihash ecosystem this is parameter-upgrade-scale insurance [Reported]. The client-side-only claim is now confirmed at the artifact level: the working `Req/` implementation realizes the constraint as a one-term change to leaf keying (i mod k), reproduces the Table 3 (200,9) wire sizes exactly (1344 to 1280 bytes), and cross-validates a C++ miner against an independent Rust verifier [Measured].

**F-A5.** The 2017 optimization plateau is explained: the solver family had reached the memory floor its data structure permits, so the plateau was completion, not stagnation. The Section 9.1 prototype premise — measuring GPU-feature headroom above the 2017 solvers — is superseded; the informative prototypes are now running the 2025 estimators against deployed parameters and benchmarking Requihash against Equihash on identical hardware [Reported; supersedes the corresponding FableFrontiers.md Section 9.1 line per the plan's supersession rule]. Building the `Req/` solver independently re-derived the 2016-17 techniques from profiling: allocation (59%) and comparison sort (24%) dominate the naive solver, and the arena-plus-incomplete-bucket-sort fixes recover 1.86x — the same layout and sort techniques the original wave found, confirming they were the reachable wins and the memory floor was real [Measured].

**F-A6.** Memory-hard PoW is entering a proven-hardness second generation: pebbling-based lower bounds on cumulative prover memory (Blocki-Smearsoll, eprint 2025/1456) replace solver-optimality conjecture, and the framework formally rehabilitates MTP — the Biryukov–Khovratovich design wounded in 2017. First-generation security postures ("memory-hard because the best-known solver needs memory") should be re-graded across the board [Reported].

**F-A7.** The GBP solver family reaches into lattice security, but as a proof technique more than a threat: Ducas–Engelberts–Loyer (Crypto 2025, [eprint 2025/575](https://eprint.iacr.org/2025/575)) prove Wagner's algorithm solves SIS-infinity in subexponential exp(O(n/log log n)) time — the first *proven* (not heuristic) subexponential Wagner bound — while explicitly noting it does not threaten Dilithium's concrete security. The significance for Topic A is that the same solver whose Equihash behavior this document analyzes is now a rigorously-characterized object on the lattice side too; the cross-topic hook is recorded in PostQuantum.md F-B10 [Reported].

**F-A8.** The design energy of the memory-hard generation has migrated to tensor puzzles (Pearl and the InferencePoW.md landscape): memory-hardness anchored puzzles to the 2016 commodity (DRAM), and the field is re-anchoring to the 2026 commodity (matrix units), with memory bandwidth surviving as the feeding constraint rather than the puzzle. Equihash's repair and the tensor generation's rise are complements, not competitors — F-X1's convergent substrate serves both [Modeled from Reported components].

**F-A9.** The governance record is as instructive as the cryptanalysis: both fatal critiques existed in 2017, no deployed chain acted on them, Zcash held (200,9) through three ASIC generations, and the repair arrived eight years later from uninvolved researchers. This is the same gatekeeper-latency pattern as PostQuantum.md F-B9, in a smaller and fully-played-out arena [Reported plus interpretive].

**F-A10.** Equihash's time-memory-tradeoff security was a claim about the wrong problem, and 2017 established this three ways: the paper's steepness propositions (plain Wagner (k−1)/2, algorithm-bound k/2) analyze the regular k-list problem, Alcock–Ren showed no tradeoff bound is proven for the loose problem Equihash actually solves, and Dinur–Nadler broke sibling MTP's steepness with a sub-linear tradeoff — no steepness claim in the family was a theorem until 2025 (F-A6). Requihash's 12x-penalty claim (F-A4) is the k/2-steepness argument applied to the regular problem it legitimately restores, and the working implementation can now measure that steepness empirically rather than assert it — the experiment 2017 says should precede any deployment [Reported; implementation path in Req/SECURITY_ANALYSIS.md §8-8a].

**F-A11.** A structural shortcut analysis of the implemented scheme ([Req/SECURITY_ANALYSIS.md](Req/SECURITY_ANALYSIS.md)) finds the regularity change is genuinely double-edged: it closes the single-list index-pointer/ASIC attack it was designed to close (L4), but the `i mod k` term introduces k low-entropy public list-classes that are a new structure-exploitation surface Equihash lacks (L7). The highest-priority open question is whether a memory-reduced solver can hold some classes resident and recompute others for a *better* tradeoff than the generic k/2 steepness (hypothesis H1) — if so, the 12x penalty of F-A4 is optimistic and k-dependent. The memory hardness is meanwhile shown to be a *representation* property, not an algorithmic one: the many-fold Equihash reduction moved no hash evaluations, only resident bytes, so any GBP PoW whose memory lives in reconstructable bookkeeping is not hard in that bookkeeping [Structural; experiments enumerated in the analysis].

## 7. Conclusions

**Level 1, macro.** Memory-hard proof-of-work as deployed failed its founding promise — ASIC resistance fell at the flagship parameters within two years, by algorithmic means the design's own optimization contest surfaced — but the verdict on the design space is mis-foundation, not impossibility: the problem was mis-specified (F-A2), the hardness that survives is real (F-A3), the repair is cheap (F-A4), and the second generation has lower-bound foundations the first lacked (F-A6). The reversal conditions: a memory-efficient linear-time self-merge algorithm would void the surviving hardness, and a practical break of the pebbling framework's assumptions would void the second generation's claims.

**Level 2, strategic.** For any Equihash chain, a Requihash-class regularity fork is the obvious insurance and its absence is now a conscious choice; the audit standard for every memory-hardness claim, in any scheme, is a hostile-data-structure review (F-X7), not a memory-total citation. Watch the Blocki-Smearsoll pebbling line (eprint 2025/1456) as the design basis for anything new, the Tang–Sun–Gong open problems as the theory frontier, and the Ducas SIS-infinity line for GBP results that move lattice-signature margins (F-A7). Treat first-generation memory-hardness claims as unsubstantiated until re-derived; treat RandomX as the surviving ASIC-resistance benchmark, since it targets execution generality rather than memory. Do not build new capacity-hard PoW against DRAM economics that F-A8 says the field has already left.

**Level 3, immediately actionable.** Run the [tl2cents estimators](https://github.com/tl2cents/Generalized-Birthday-Problem) against every deployed Equihash parameter set — Zcash (200,9), Bitcoin Gold (144,5), and the (150,5), (192,7), (96,5) family across smaller chains — and publish the corrected time/memory table with the k-bound check; artifact: a table and script extending this document. Extend the existing `Req/` benchmark harness — which already reproduces the Table 3 wire sizes and profiles the solver at small parameters [Measured] — to (200,2^9) on one GPU once compact index-pointer storage lands, verifying the 84.4x/4.6x time/memory trade and the 49 MB baseline against shipping miners; artifact: the (200,9) rows of Table 3 upgraded from Reported to Measured. Draft the ZIP-style specification a Requihash deployment would need — constraint encoding, canonical ordering, difficulty retune, packet structure for the omitted index field; artifact: a specification note, the concrete deliverable F-A4 implies.

## 8. Further Directions

Directions that survive these findings but exceed this document's scope, per the FableFrontiers.md Section 7 taxonomy.

*Science.* A lower bound for the self-merge problem is the load-bearing open question — Equihash-family hardness is exactly its hardness (F-A3). The Tang–Sun–Gong open problems, especially sub-2^(n/2) algorithms for non-constant-k k-SUM and the unoptimized k window up to √(n+1), define the near frontier; pebbling lower bounds under realistic memory models (banked DRAM, HBM channel structure) would connect the 2025 pebbling line to actual mining hardware; quantum k-XOR with honest QRAM cost accounting remains open.

*Engineering.* A reference Requihash implementation now exists in `Req/` (C++ and Rust, cross-validated, with a multi-backend solver/verifier structure and a benchmark harness); what remains for production is compact index-pointer storage — the one canonical 2016-17 technique not yet ported, and the (2^k)/k space win that makes (200,9) mining feasible — plus the difficulty-retune tooling and a real SIMD/GPU miner. Integrating the 2025 complexity estimators into a parameter-selection tool would give every GBP-based scheme what Equihash never had — a validity checker. The Bai et al. adversary-solver methodology deserves replication against Requihash to test the 12x penalty claim adversarially, which the working implementation now makes possible. The specific TMTO experiments — a memory-capped (Bernstein-truncation) solver backend to measure empirical steepness on the regular Requihash problem, an Equihash-vs-Requihash steepness comparison, and a Dinur–Nadler-style adversarial pass attempting to beat the memory floor — are the highest-value unbuilt tests, laid out in [Req/SECURITY_ANALYSIS.md](Req/SECURITY_ANALYSIS.md) §8-8a; no one has measured tradeoff steepness for the regular problem the k/2 claim actually concerns.

*Business.* The Equihash ecosystem's security debt is quantified and the repair is priced: upgrade engineering, audit, and difficulty-transition management for the affected chains is a concrete, deadline-shaped consulting surface, with the alternative — status quo on a formally weakened puzzle securing billions — now documented in the open literature. Estimator-driven parameter audits generalize to every GBP-adjacent scheme, including incremental hashes in production systems (the iSHAKE-class exposure).

*Social.* A collapsed memory barrier is a collapsed attack cost: chains that keep first-generation parameters are cheaper to 51%-attack than their holders believe, and the information asymmetry between the cryptanalytic literature and coinholder awareness is itself a harm vector. The dual-use lesson of F-A9 — optimization contests surface attack surfaces — belongs in the design playbook of any project that crowdsources performance work on security-critical components.

## 9. References

The defining papers and primary sites for Requihash's own construction,
the Equihash/GBP lineage it repairs, and the hash primitives it builds
on — listed and linked once here, **sorted most recent year first**.
Other documents in this project cite these by **Author (Year)** and point
back here rather than repeating title, venue, and link details inline.
This is not an exhaustive bibliography — narrower or more tangential
sources stay cited inline wherever they're actually used (e.g. this
document's own §2 timeline table, `SOLVERS.md`'s commit-level provenance);
this list is only the sources load-bearing enough to be cited from more
than one place. Full unpacked (not just cited) treatment of each paper
lives in `PAPERS.md`, not repeated here.

Link preference: a stable URL (eprint/arXiv/RFC/vendor site) wherever one
exists — every entry below has one. An exact local filename would be used
as a fallback only where no such URL exists; none of the entries here need
that fallback as of this writing.

- **Tang, Ding, Sun, Gong (2025).** "Memory Optimizations of Wagner's
  Algorithm with Applications to Equihash." eprint 2025/2141.
  [Paper](https://eprint.iacr.org/2025/2141),
  [artifact repo](https://github.com/tl2cents/Wagner-Algorithms).
  List Item Reduction (LIR) techniques; concrete implementation numbers for
  plain Equihash (144,5) at 700 MB-2.5 GB depending on time-penalty
  tradeoff, explicitly attributed against Tromp's own solver as baseline.
- **Blocki & Smearsoll (2025).** "Provably Memory-Hard Proofs of Work With
  Memory-Easy Verification." eprint 2025/1456.
  [Paper](https://eprint.iacr.org/2025/1456).
  Formally rehabilitates Biryukov & Khovratovich's MTP framework (broken by
  Dinur & Nadler below) when instantiated with a data-independent
  memory-hard function.
- **Tang, Sun, Gong (2025).** "On the Regularity of the Generalized
  Birthday Problem." eprint 2025/1351.
  [Paper](https://eprint.iacr.org/2025/1351),
  [artifact repo](https://github.com/tl2cents/Generalized-Birthday-Problem).
  **The paper this project's Requihash implementation is based on** — the
  regular (k-list) vs. loose (single-list) GBP distinction, the parameter
  validity bound, the regularity-repaired construction (the paper's own
  name: Sequihash). Table 3 (page 31) is this project's primary source for
  published Equihash/Sequihash memory, time, and solution-size figures
  (`Req/SIZING.md`).
- **Ducas, Engelberts, Loyer (2025).** "Wagner's Algorithm Provably Runs in
  Subexponential Time for SIS∞." eprint 2025/575.
  [Paper](https://eprint.iacr.org/2025/575).
  Proves Wagner's algorithm solves SIS^∞ (underlying Dilithium's concrete
  security) in subexponential time — does not threaten Dilithium in
  practice at real parameters.
- **Bai, Gao, Hu, Zhang (2019).** "Constructing an Adversary Solver for
  Equihash." NDSS 2019.
  [Paper](https://www.ndss-symposium.org/ndss-paper/constructing-an-adversary-solver-for-equihash/).
  The off-chip, multi-chip ASIC design built on the index-pointer
  optimization — Equihash's ASIC-resistance defeat, formalized as a
  parameter-independent adversary solver.
- **O'Connor, Aumasson, Neves, Wilcox-O'Hearn (2019).** "BLAKE3: one
  function, fast everywhere."
  [Spec repo](https://github.com/BLAKE3-team/BLAKE3-specs),
  [implementation repo](https://github.com/BLAKE3-team/BLAKE3). The design
  paper and official Rust/C implementations behind `hash=blake3`
  (`Req/SPEC.md` §6); local clones `~/Work/ZK/ZKs/BLAKE3-specs`,
  `~/Work/ZK/ZKs/BLAKE3`.
- **Alcock & Ren (2017).** "A Note on the Security of Equihash." CCSW 2017
  (ACM Cloud Computing Security Workshop).
  [ACM listing](https://dl.acm.org/doi/10.1145/3140649.3140652).
  First public flag that Equihash's security does not actually reduce to
  Wagner's algorithm as claimed, and that no tradeoff-resistance bound is
  proven for it — the crack the 2025 Requihash/Sequihash line above
  eventually repairs formally.
- **Dinur & Nadler (2017).** "Time-Memory Tradeoff Attacks on the MTP
  Proof-of-Work Scheme." eprint 2017/497.
  [Paper](https://eprint.iacr.org/2017/497).
  The sub-linear computation-memory tradeoff break of MTP that motivates
  Blocki & Smearsoll's data-independence requirement above.
- **Biryukov & Khovratovich (2016).** "Equihash: Asymmetric Proof-of-Work
  Based on the Generalized Birthday Problem." NDSS 2016; journal version
  Ledger 2017. [NDSS paper](https://www.internetsociety.org/sites/default/files/blogs-media/equihash-asymmetric-proof-of-work-based-generalized-birthday-problem.pdf),
  [eprint 2015/946](https://eprint.iacr.org/2015/946),
  [Ledger journal version](https://ledger.pitt.edu/ojs/ledger/article/view/48),
  [reference implementation](https://github.com/khovratovich/equihash).
  **The paper this entire document's subject matter descends from.**
  Defines Equihash: memory-hard PoW via a single-list variant of Wagner's
  algorithm plus algorithm binding against amortization. Adopted by Zcash
  at (200,9). RK (`Req/SOLVER_CORPUS.md`) ports this paper's own reference
  solver.
- **Aumasson, Neves, Wilcox-O'Hearn, Winnerlein (2013).** "BLAKE2: simpler,
  smaller, fast as MD5."
  [Paper](https://www.blake2.net/blake2.pdf), [site](https://blake2.net).
  The design paper behind `hash=blake2b` (`Req/SPEC.md` §3). Distinct from
  the IETF RFC 7693 (Saarinen & Aumasson, 2015,
  [text](https://www.rfc-editor.org/rfc/rfc7693)) — that is the
  standards-track document; this is the original design write-up. The
  separately-maintained reference implementation package
  ([github.com/BLAKE2/BLAKE2](https://github.com/BLAKE2/BLAKE2) — portable
  C, x86-vectorized C, ARM NEON, POWER8 variants; local clone
  `~/Work/ZK/ZKs/blake2-reference`) is the more commonly deployed reference
  shape and differs from the RFC's own sample code in struct layout and
  call signatures.
- **Bernstein, Lange, Niederhagen, Peters, Schwabe (2011).** "FSBday:
  Implementing Wagner's generalized birthday attack against the SHA-3
  round-1 candidate FSB."
  [Paper](https://cr.yp.to/papers.html#fsbday).
  A real, executed implementation of Wagner's attack against an actual
  SHA-3 candidate — a concrete "naive formula vs. real achievable
  footprint" precedent outside the Equihash/Sequihash line.
- **Bernstein (2007).** "Better price-performance ratios for generalized
  birthday attacks."
  [Paper](https://cr.yp.to/papers.html#genbday).
  Improves the machine-size/time exponents on Wagner's original attack.
- **Wagner (2002).** "A Generalized Birthday Problem." CRYPTO 2002.
  [Author's listing](https://people.eecs.berkeley.edu/~daw/papers/genbday.html).
  No local PDF located in this project's reference directories as of this
  check (`Req/SIZING.md` §0a notes the same). **The foundational result
  everything else in this list is downstream of** — the k-tree algorithm,
  sub-birthday-bound solving of the k-list XOR problem. Equihash's
  single-list construction (above) is explicitly presented by its authors
  as based on this algorithm, which is the specific claim Tang, Sun, Gong
  (2025, above) show is a mischaracterization.

**Where these are used:** `Req/SPEC.md` — Biryukov & Khovratovich (2016);
Aumasson et al. (2013); Tang, Sun, Gong (2025). `Req/SIZING.md` — Tang,
Sun, Gong (2025) (primary source for Table 3); Tang, Ding, Sun, Gong
(2025); Bernstein (2007); Bernstein et al. (2011); Wagner (2002).
`Req/SOLVER_CORPUS.md` — Biryukov & Khovratovich (2016) (RK's source);
Tang, Sun, Gong (2025). `PAPERS.md` — full unpacked coverage of all nine
papers above. `SOLVERS.md` — Biryukov & Khovratovich (2016), xenoncat's
own algorithm description PDF (a primary source, not a paper in this list
— see `SOLVERS.md` §0.2), Alcock & Ren (2017), Bai et al. (2019).
