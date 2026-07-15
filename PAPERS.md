# PAPERS.md — definitive citation index for research papers relevant to this project

**The single source of truth for research-paper citations across this
project.** Sections 1-9 give full, unpacked (not just cited) treatment of
the nine papers most directly relevant to Equihash/Sequihash/Requihash —
propositions, exact quotes, tables, page numbers. Section 10 gives shorter,
formally-linked citations for other research articles in scope (referenced
in `Equihash.md`'s narrative, or as background the big nine build on)
without the same depth of unpacking. Other documents in this project —
`Equihash.md`, `Req/SIZING.md`, `Req/SOLVER_CORPUS.md`, `Req/SPEC.md`,
`SOLVERS.md`, RZ's own `STATUS.md` — cite research papers by short
designator (e.g. "Tang, Sun, Gong (2025)") and point back here rather than
repeating title/venue/link details inline; a document's own References
section (e.g. `Equihash.md` §9) retains only mentions and links that are
**not** already listed here — non-paper sites, tools, and repos.

The 2025-dated entries in §1-4 were obtained as PDFs
(`~/Downloads/2025-575.pdf`, `2025-1351.pdf`, `2025-1456.pdf`,
`2025-2141.pdf`) and scanned directly (not from secondary summaries), first
pass 2026-07-14; titles, authors, and venue status for those are
transcribed from each PDF's own title page and/or the eprint listing
metadata, checked live against `eprint.iacr.org`. §1-9 are **ordered most
recent first** by publication year, ties broken by eprint received date
where known (not eprint number, and not latest-revision date — a revision
updates the same paper, it isn't a new one); §10 likewise, most recent
first.

## 1. Memory Optimizations of Wagner's Algorithm with Applications to Equihash

**Received:** 2025-11-23. **Latest revision:** 2026-05-30 (3rd revision).
**Authors:** Lili Tang, Rui Ding, Yao Sun, Xiaorui Gong (School of Cyber
Security, University of Chinese Academy of Sciences; Institute of
Information Engineering, Chinese Academy of Sciences, Beijing, China — same
two institutions as paper 3 below; Rui Ding added as a fourth author, not
present on paper 3).
**eprint:** [2025/2141](https://eprint.iacr.org/2025/2141). Status: eprint
listing only as of this check.
**Artifact repo:** [tl2cents/Wagner-Algorithms](https://github.com/tl2cents/Wagner-Algorithms)
("Memory Optimizations of single-list Wagner's Algorithm"; created
2025-10-04, last pushed 2026-02-21) — **a second, separate repository from
paper 3's**, not yet cloned or run by this project.

A practically-oriented companion to paper 3, focused specifically on List
Item Reduction (LIR) techniques — reducing the amortized cost of storing
indices and hash values per list item, as opposed to List Size Reduction
(LSR, reducing the list size itself, which the paper shows incurs
prohibitive time penalties for Equihash specifically — e.g. halving
Equihash(200,9)'s memory via the state-of-the-art LSR trade-off costs a time
penalty factor of 2^24.6). Introduces a hybrid framework achieving,
per the abstract, "more than 50%" peak memory reduction (from >2nN to nN
bits) at roughly a twofold time penalty, across all Equihash parameters.
**Concrete, directly-quoted implementation numbers, distinct from paper 3's
theoretical Table 3** (see `Req/SIZING.md` §4 for the full quote and
context): *"For Equihash(144,5), our optimized algorithm requires only
700 MB of memory, compared to approximately 2.5 GB in previous
implementations"* — the "previous implementations" baseline is explicitly
attributed by name to **Tromp**'s own (144,5) solver. The paper's baseline
(non-time-penalized) implementation for the same parameters uses 1.45 GB
(0.57× of the cited Tromp figure); the further-optimized version with a ~2×
time penalty reaches 700 MB (0.28×). As of December 2025 (paper's own
figure), Zcash alone had a market capitalization exceeding $6 billion — cited
by the paper as part of its motivation for why Equihash memory-hardness
still matters practically.

**Extract — the actual techniques behind the headline numbers** (read
directly from the PDF, §1.2/§4, not inferred): the paper distinguishes List
Size Reduction (LSR — shrinking the input list, which existing work already
covers and which the paper shows is expensive: halving Equihash(200,9)'s
memory via the state-of-the-art LSR bound costs a 2^24.6 time-penalty
factor) from **List Item Reduction (LIR)** — its own contribution, which
instead shrinks the per-item storage cost (hash value + index vector) at
each layer above the first, since that storage "grows exponentially with
the layer height" in the naive single-chain algorithm (Algorithm 1, CIV).
Three concrete techniques, each with a paper-stated bound:
- **CIV-IT** (Constrained Index Vector, Index Trimming): bounds peak memory
  by `max(nN, (2^k + 2ℓ)N)` bits, time penalty factor ≈2 — a "constrained
  single-chain algorithm to reconstruct the full solution vector in the
  second run."
- **CIP-PR** (Constrained Index Pointer, Post-Retrieval): reduces peak
  memory from ~`2nN` to `nN` bits, time penalty factor ≈`1 + k/8` — "when
  external-memory caching is available... executed with `nN` bits of
  memory and a negligible time overhead in practice."
- **Hybrid** (§3.3): combines both; the paper's own Table 1 gives measured
  `Mem./Time` at seven Equihash `(n,k)` points for CIP (baseline),
  CIV-IT, CIP-PR, and Hybrid side by side, plus a separate `k`-tree
  (TIV/TIV-IT) column — e.g. at (200,9): CIP `2^29.55`, CIV-IT `2^29.21 /
  1.4·T0`, CIP-PR `2^28.64 / 2.7·T0`, Hybrid `2^28.38 / 4.0·T0`; at
  (144,5): CIP `2^32.95`, CIV-IT `2^31.64 / 2.3·T0`, CIP-PR `2^32.17 /
  2.2·T0`, Hybrid `2^31.36 / 3.8·T0`. (`T0`/`T1` are the plain-CIP/plain-TIV
  runtimes; `q = M0/M1` is the memory-reduction factor, `γ = T1/T0` the time
  penalty — both terms defined in §1.2, reused as-is here rather than
  re-derived.)
- Figure 1 gives the optimal LIR "switching height" as a closed form,
  `h* = (k+1)/2` — beyond which "no further memory benefits can be
  obtained in theory," a specific, checkable claim worth having on file if
  this project ever implements a LIR-style backend.
- The 700 MB/1.45 GB/2.5 GB figures quoted above are this paper's own
  measured numbers for the *plain Equihash* algorithm (not Requihash/
  Sequihash) — a genuinely separate implementation-optimization result from
  paper 3's Requihash construction below, not a re-measurement of it; see
  the Cross-paper notes for why nobody has yet run this paper's own code
  against paper 3's Requihash.

## 2. Provably Memory-Hard Proofs of Work With Memory-Easy Verification

**Received:** 2025-08-11. **Latest revision:** 2025-08-12 (approved, no
further revisions since).
**Authors:** Jeremiah Blocki, Nathan Smearsoll (Purdue University, West
Lafayette, IN).
**eprint:** [2025/1456](https://eprint.iacr.org/2025/1456). Status: eprint
listing only as of this check.

Revisits Biryukov and Khovratovich's Merkle Tree Proofs (MTP) memory-hard
proof-of-work candidate (USENIX 2016), built on the Argon2d memory-hard
function — a construction Dinur and Nadler (eprint 2017/497, cited in
`Equihash.md`'s timeline) broke, exploiting data-dependencies in the
underlying Argon2d graph. This paper formally proves, in the parallel
random oracle model, that the MTP framework *is* sound when instantiated
with a suitably *data-independent* memory-hard function, generically
lower-bounding any prover's cumulative memory cost by the pebbling cost of
the construction's "ex-post-facto" graph. When instantiated with DRSample,
the resulting scheme has an honest prover running in sequential time O(N),
proof size and verification time polylog(N), and any malicious prover
forced to Ω(N²/log N) cumulative memory complexity. Also develops general
pebbling attacks showing any iMHF-based MHPoW using the MTP framework has
proof size at least Ω(log²N/log log N), and at least Õ(N^0.32) when
instantiated with Argon2i specifically. Relevant to this project's broader
interest in provably-secure (not just conjectured-secure) memory-hard
constructions, as a contrast case to Equihash/Sequihash's security
argument, which (per paper 3) rested on an unproven — and, per that paper,
incorrect — problem-equivalence assumption for two decades.

## 3. On the Regularity of the Generalized Birthday Problem

**Received:** 2025-07-24. **Latest revision:** 2026-05-30 (5th revision).
**Authors:** Lili Tang, Yao Sun, Xiaorui Gong (School of Cyber Security,
University of Chinese Academy of Sciences; Institute of Information
Engineering, Chinese Academy of Sciences, Beijing, China).
**eprint:** [2025/1351](https://eprint.iacr.org/2025/1351). Status: eprint
listing only as of this check; the acknowledgments record submission to
Asiacrypt, Eurocrypt, and Crypto ("we received insightful comments from
anonymous reviewers of Asiacrypt, Eurocrypt and Crypto"), so it has been
through peer review at one or more of those venues without (as far as this
check found) a confirmed acceptance.
**Artifact repo:** [tl2cents/Generalized-Birthday-Problem](https://github.com/tl2cents/Generalized-Birthday-Problem)
(Python, Sagemath 10.6; created 2025-07-24, last pushed 2026-05-31).

The paper this project's Requihash implementation is based on, and the
source of the "Sequihash" name (§0 of `Req/SIZING.md`). Formalizes the
regular (k-list, RGBP) vs. loose (single-list, LGBP) generalized birthday
problem distinction that Equihash's original design conflated; derives a
√2-factor complexity-exponent gap between them; establishes the parameter
validity bound k ≤ √(n/2+1); analyzes the self-merge problem as Equihash's
surviving hardness after the index-pointer defeat; mounts a new collision
attack on the iSHAKE incremental hash (reducing its security bound from
2^256 to 2^189); and proposes the regularity-repaired construction the paper
calls Sequihash. Table 3 (page 31) is this project's primary source for
published Equihash/Sequihash memory, time, and solution-size figures — see
`Req/SIZING.md` §0a for a full account of an error this project made citing
it (a companion notebook formula was mistaken for the table's own figures)
and the correction.

**Extract — the two propositions and the actual construction, read directly
from §5 of the PDF** (this is the load-bearing part of the paper for this
project's own code, so worth having the exact statements on file rather
than only the narrative summary above):
- **Proposition 3** (complexity of the k-list algorithm, the one Requihash
  restores validity for): index-trimmed RGBP(n, 2^k) runs in time
  `O(2^k · N)` and requires peak memory of at least
  `O( ((k²+5k+2)/4 · ℓ + 2^(k-1)) · N )` bits.
- **Proposition 4** (complexity of the single-list algorithm, i.e. plain
  Equihash as actually mined): index-pointer LGBP(n, 2^k) runs in time
  `O(k·N)` and requires peak memory of at least `O(n · N)` bits — this is
  the formula `Req/SIZING.md` §1 already cites as the one that reproduces
  Table 3's Equihash column exactly (constant 1), confirmed independently
  in that document; Proposition 3 is the corresponding formula for the
  Requihash/Sequihash column, likewise already the one `SIZING.md` uses.
- **Proposal 1** (Regular Equihash, i.e. Requihash/Sequihash itself — the
  actual construction, verbatim): given `ℓ = n/(k+1)`, `N = 2^ℓ`, hash
  `H: {0,1}* → {0,1}^n`, the prover picks seed `I` (e.g. the working
  block's hash), finds nonce `V` and `(ℓ+k)`-bit `x_1,...,x_K` such that
  `H(I‖V‖x_1) ⊕ H(I‖V‖x_2) ⊕ ... ⊕ H(I‖V‖x_K) = 0` **and**
  `x_i ≡ i-1 (mod K)` for all `i ∈ [1,K]` — i.e. the sequential
  class-binding constraint, matching `Req/README.md`'s own "What Requihash
  changes" section's `i mod k` framing (this project's `i`/`x_i` naming
  differs cosmetically from the paper's, already flagged as a design
  choice in `Req/SPEC.md`, not a discrepancy in the underlying constraint).
- The paper's own stated headline tradeoff (§5.2, restated in prose above
  Table 3): "For all parameters `(n,k)` of Equihash with `k ≥ 5`... [Requihash]
  can increase the peak memory by at least 100%, without incurring
  drawbacks... Choosing larger values of `k` further amplifies the
  advantage." At (200,9) specifically: solution size drops slightly (1344 →
  1280 bytes) while time complexity rises 84.4× and memory complexity rises
  4.6× (49 MB → 223 MB) — the exact numbers `Req/SIZING.md` §2b already
  transcribes in full table form, not repeated again here.

**A second, closely related paper sharing most of the same authors** (see
paper 1 above, chronologically later but listed earlier in this document
since it's the more recent of the two) revisits the same memory-hardness
question from an implementation-optimization angle rather than a theory
angle.

## 4. Wagner's Algorithm Provably Runs in Subexponential Time for SIS∞

**Received:** 2025-03-29. **Latest revision:** 2025-04-01 (approved, no
further revisions since).
**Authors:** Léo Ducas (CWI & Leiden University, Netherlands), Lynn
Engelberts (CWI & QuSoft, Netherlands), Johanna Loyer (CWI, Netherlands).
**eprint:** [2025/575](https://eprint.iacr.org/2025/575). Status: eprint
listing only as of this check.

Re-derives Wagner's k-list algorithm as a walk backward through a chain of
projected lattices and superlattices, with Gaussian-randomized-rounding
bucketing in place of Wagner's original discrete bucketing. Proves — not
merely conjectures — that this variant of Wagner's algorithm solves the
Short Integer Solution problem in the infinity norm (SIS^∞) in subexponential
time exp(O(n / log log n)), for modulus q = poly(n), requiring only
m = n + ω(n / log log n) samples. SIS^∞ underlies the concrete security of
Dilithium, NIST's standardized post-quantum signature scheme; the paper's
own stated conclusion is that despite the subexponential complexity, the
algorithm — even with all provability overhead stripped — is far less
efficient against Dilithium's actual concrete parameters than standard
lattice-reduction attacks, so **this result does not threaten Dilithium in
practice**. Relevant to this project as a second, independent 2025 result on
Wagner's algorithm's own theoretical limits — a different question (lattice
attack surface) from the memory-hardness/GBP-regularity question papers 1
and 3 address, but the same underlying algorithm. The earliest-received
2025 entry in this document (2025-03-29) — the entries below predate 2025
entirely and are ordered by publication year, not received date.

## 5. Constructing an Adversary Solver for Equihash

**Published:** NDSS 2019 (24-27 February 2019, San Diego, CA).
**Authors:** Xiaofei Bai, Jian Gao, Chenglong Hu, Liang Zhang (School of
Computer Science, Fudan University; Shanghai Key Laboratory of Data
Science, Fudan University; Shanghai Institute of Intelligent Electronics &
Systems).
**Paper:** [ndss-symposium.org listing](https://www.ndss-symposium.org/ndss-paper/constructing-an-adversary-solver-for-equihash/).

Equihash's ASIC-resistance defeat, formalized: a **parameter-independent**
adversary solver design, constructed by dissecting Equihash's software
solving algorithm into subroutines with different memory characteristics
(capacity vs. bandwidth) and mapping each onto a small chip separately,
rather than a single monolithic ASIC. The paper's own stated result:
simulation with a 28nm library shows up to 90% reduction in computation
power compared to contemporary GPUs (under 12nm process), projecting **at
least a 10x efficiency advantage** for resourceful adversaries — a smaller
edge than typical ASIC-vs-commodity gaps (2-3x is cited as the threshold
the paper considers dangerous for decentralization), but the paper argues
even this smaller edge is enough to centralize and weaken a PoW-based
blockchain because Equihash's memory usage "can be dissected into
subroutines with different characteristics and handled accordingly."
Explicitly framed as a security-inspection exercise (construct the attack,
publish it, motivate parameter/design reassessment) rather than a deployed
attack. The regularity binding Requihash/Sequihash (paper 3 above) adds is
a structural response to exactly this kind of subroutine-decomposition
attack surface — it disables the single-list index-pointer optimization
this paper's adversary design (and the index-pointer technique generally)
depends on.

## 6. A Note on the Security of Equihash

**Published:** CCSW 2017 (ACM Cloud Computing Security Workshop).
**Authors:** Alcock, Ren.
**Paper:** [ACM listing](https://dl.acm.org/doi/10.1145/3140649.3140652).

The first public flag — two years before this project's own primary
source paper (Tang, Sun, Gong 2025) formalized the fix — that Equihash's
claimed security does not actually reduce to Wagner's generalized-birthday
hardness the way the original design paper (Biryukov & Khovratovich 2016,
below) asserts: no tradeoff-resistance bound is known for Equihash as
specified, and the paper's own analysis of the expected solution count
is shown to be incorrect. The paper's stated purpose is not to demonstrate
an immediate practical attack but to establish that Equihash should be
treated as a heuristic scheme with no formally proven security guarantee
— directly the gap that Tang, Sun, Gong's 2025 regular/loose GBP
distinction (Proposition 3/4, `Req/SIZING.md`) closes with an actual proof
structure, eight years later.

## 7. Time-Memory Tradeoff Attacks on the MTP Proof-of-Work Scheme

**Published:** eprint 2017/497.
**Authors:** Itai Dinur, Niv Nadler (Department of Computer Science,
Ben-Gurion University, Israel).
**Paper:** [eprint 2017/497](https://eprint.iacr.org/2017/497).

Not an Equihash paper directly, but the historical precedent motivating
paper 2 above (Blocki & Smearsoll 2025): a sub-linear computation-memory
tradeoff attack on Biryukov & Khovratovich's *other* 2016 memory-hard PoW
proposal, MTP (Merkle Tree Proof, USENIX Security 2016), applied to the
designers' own concrete instance (Argon2d, 2 GB allocated memory). Computes
arbitrary malicious proofs using under 1 MB of memory (~1/3000 of the
honest prover's) at a computational penalty of only 170× — over 55,000×
faster than the designers' own claimed penalty bound — after a one-time
2^64 precomputation step. The mechanism: Argon2d's memory access pattern is
*data-dependent* (each access determined by prior computation), letting the
attacker inject a small number of carefully chosen memory blocks to
manipulate that access pattern. This is exactly the break that motivates
paper 2's insistence on a *data-independent* memory-hard function
(DRSample) as the fix, cited there but unpacked here since it is itself a
real, standalone contribution to the memory-hard-PoW security literature
this project tracks.

## 8. Equihash: Asymmetric Proof-of-Work Based on the Generalized Birthday Problem

**Published:** NDSS 2016; journal version Ledger 2017.
**Authors:** Alex Biryukov, Dmitry Khovratovich.
**Paper:** [NDSS PDF](https://www.internetsociety.org/sites/default/files/blogs-media/equihash-asymmetric-proof-of-work-based-generalized-birthday-problem.pdf),
[eprint 2015/946](https://eprint.iacr.org/2015/946),
[Ledger journal version](https://ledger.pitt.edu/ojs/ledger/article/view/48),
[reference implementation](https://github.com/khovratovich/equihash).

**The paper this entire project's subject matter descends from.** Defines
Equihash: a memory-hard proof-of-work built on a single-list variant of
Wagner's generalized-birthday algorithm (below), with *algorithm binding*
— structural constraints (the tree-ordering and distinct-index rules
`Req/SPEC.md` §7 still implements) added specifically to prevent
amortization and to force parallel implementations to be memory-bandwidth-
bound, not just memory-capacity-bound. Adopted by Zcash at (n,k)=(200,9)
and by numerous forks at other parameter points (Bitcoin Gold at (144,5),
Zero Currency at (192,7)). The paper's own claimed security argument —
that solving Equihash reduces to Wagner's hardness — is the claim Alcock &
Ren (2017, above) first showed doesn't actually hold, and that Tang, Sun,
Gong (2025) later diagnosed precisely (the single-list construction
solves a *loose* GBP, not the *regular* k-list GBP Wagner's algorithm
targets) and repaired (Requihash/Sequihash). RK (`Req/SOLVER_CORPUS.md`)
ports this paper's own reference solver (Khovratovich's C++, from the
paper's companion repo) directly.

## 9. A Generalized Birthday Problem

**Published:** CRYPTO 2002.
**Author:** David Wagner.
**Paper:** [author's listing](https://people.eecs.berkeley.edu/~daw/papers/genbday.html).
No local PDF located in this project's reference directories as of this
check (`Req/SIZING.md` §0a notes the same).

**The foundational result everything else in this document is downstream
of.** Generalizes birthday-paradox collision search from 2 lists to `k`
lists: given `k` lists of `n`-bit random values, find one element from
each list whose XOR (or modular sum) is zero, solvable in
sub-birthday-bound time via a binary merge tree — pairwise-merge lists
that agree on a shrinking prefix of bits, halving the list count each
round while the collision constraint tightens by the same number of bits
per round, until one list of expected size ~1 remains at the root.
Equihash's single-list construction (paper 8, above) is explicitly
presented by its authors as *based on* this algorithm, which is the
specific claim Tang, Sun, Gong (2025) show is a mischaracterization: the
single-list problem Equihash actually solves (LGBP) is a different,
looser problem than the regular k-list GBP Wagner's paper defines and
solves (RGBP) — the √2-factor complexity-exponent gap between the two is
this project's own primary source's central technical finding. Bernstein
(2007) and Bernstein et al.'s FSBday (2011), below in §10, are a direct
algorithmic improvement and a real implementation of this same paper's
attack, respectively — not unpacked at the same depth as the big nine
above since neither is Equihash-specific.

## 10. Other research articles in scope

Formal citations for papers this project's documents reference — in
`Equihash.md`'s §2 timeline, in the propositions/formulas the big nine
above build on, or in background this project has read directly — that
aren't among the nine papers unpacked above. Shorter treatment than §1-9
by design: these are cited because they're relevant context, not because
this project's own work depends on their specific results the way it
depends on, say, Tang–Sun–Gong (2025). Ordered by publication year, most
recent first.

- **Esser & Santini (2024).** "Not Just Regular Decoding: Asymptotics and
  Improvements of Regular Syndrome Decoding Attacks." CRYPTO 2024.
  [eprint 2023/1568](https://eprint.iacr.org/2023/1568).
  Regularity-structured syndrome-decoding attacks — part of the
  "regularity toolkit and conjecture landscape" (`Equihash.md` §2) the
  2025 Requihash/Sequihash rework's own regular-vs-loose GBP framing
  builds on conceptually, though this paper addresses a different
  underlying problem (syndrome decoding, not GBP).
- **Dinur, Keller, Klein (2024).** "Fine-Grained Cryptanalysis: Tight
  Conditional Bounds for Dense k-SUM and k-XOR." Journal of the ACM 71(3),
  Article 23 (DOI 10.1145/3653014); originally FOCS 2021.
  [eprint 2021/1460](https://eprint.iacr.org/2021/1460).
  Proves known k-SUM/k-XOR algorithms are essentially optimal in the dense
  regime for `k=3,4,5`, and proves the k-tree algorithm's optimality over
  a limited parameter range for `k>5` — the fine-grained complexity
  landscape Tang, Sun, Gong (2025, §3 above) situates its own regular/loose
  GBP complexity-gap result against.
- **Ren & Devadas (2017).** "Bandwidth Hard Functions for ASIC
  Resistance." [eprint 2017/225](https://eprint.iacr.org/2017/225).
  Proposes bandwidth-hardness — ASIC resistance via runtime energy cost
  when available cache is insufficient — as a distinct hardness style from
  memory-capacity hardness. Cited in `Equihash.md` §2's comparison table:
  Autolykos's bandwidth-hardness claim is explicitly scoped by this
  paper's own theory as capping, not eliminating, ASIC advantage.
- **Levieil & Fouque (2006).** "An Improved LPN Algorithm." SCN 2006.
  [paper (author's site)](https://www.di.ens.fr/~fouque/pub/scn06.pdf).
  Introduces the single-list collision-search technique (their "LF2"
  method: partition samples sharing a bit-chunk, sum every in-group pair)
  for the LPN problem — the paper Tang, Ding, Sun, Gong (2025, §1 above)
  and Tang, Sun, Gong (2025, §3 above) both credit as the origin of the
  single-chain/single-list algorithm Equihash's design later reused for a
  different problem (GBP, not LPN) without preserving Wagner's original
  regularity structure.
- **Bernstein (2007).** "Better price-performance ratios for generalized
  birthday attacks." [paper](https://cr.yp.to/papers.html#genbday).
  Improves the machine-size/time exponents on Wagner's original GBP
  attack (§9 above) — a direct algorithmic improvement, not an
  Equihash-specific result.
- **Bernstein, Lange, Niederhagen, Peters, Schwabe (2011).** "FSBday:
  Implementing Wagner's generalized birthday attack against the SHA-3
  round-1 candidate FSB." [paper](https://cr.yp.to/papers.html#fsbday).
  A real, executed implementation of Wagner's GBP attack (§9 above)
  against an actual SHA-3 candidate — a concrete "naive formula vs. real
  achievable footprint" precedent outside the Equihash/Sequihash line,
  also cited in `Req/SIZING.md` §0a.

## Cross-paper notes

- Papers 1 and 3 share three authors (Tang, Sun, Gong) and are best read as
  a matched theory/practice pair: paper 3 established that Equihash's
  index-pointer defeat rests on a mis-specified problem and proposed the
  regularity fix (Sequihash/Requihash); paper 1 shows, independently of that
  fix, how much of the *original* Equihash's own memory footprint can still
  be optimized away with list-item-reduction techniques the field hadn't
  systematically explored before. Both bear directly on the D3 evidence
  question this project is building toward: paper 1's numbers say Equihash's
  memory-hardness has more give in *both* directions than assumed — attacker
  optimizations already exist (index pointers, and now LIR) and so does a
  principled repair (Requihash/Sequihash), and neither this project nor (as
  far as verified) anyone else has yet run paper 1's actual code against
  paper 3's Requihash construction to see how LIR-style optimizations would
  affect Requihash's claimed 2.3–4.9× memory penalty over plain Equihash.
- Papers 2 and 4 are not by the Sequihash authors and address different
  questions (a competing memory-hard PoW framework's own provable-security
  gap; lattice cryptanalysis) but were bundled with this reading pass
  because they're 2025-dated, adjacent-topic (memory-hard PoW provable
  security; Wagner's algorithm) results worth having cited in one place.
- **Papers 1-4 (2025-dated) are eprint-only as of this check** (2026-07-14)
  — none has a confirmed peer-reviewed venue acceptance, though paper 3's
  acknowledgments indicate at least one round of review at a major venue.
  **Papers 5-9 (2002-2019) are all confirmed peer-reviewed venue
  publications** (NDSS, CCSW, CRYPTO) — the older end of this list is
  fully published, not eprint-pending.
- **The full nine-paper set traces one continuous argument.** Wagner (9,
  2002) defines the k-list GBP and its solving algorithm. Biryukov &
  Khovratovich (8, 2016) build Equihash on a single-list variant, claiming
  it inherits Wagner's hardness. Alcock & Ren (6, 2017) are first to show
  that claim doesn't actually hold. Bai et al. (5, 2019) demonstrate the
  practical consequence — a parameter-independent adversary ASIC exploiting
  exactly the structural gap Alcock & Ren flagged. Tang, Sun, Gong (3,
  2025) diagnose the gap precisely (LGBP vs. RGBP, a proven complexity
  difference) and repair it (Requihash/Sequihash's regularity binding,
  which is specifically designed to defeat the index-pointer technique
  Bai et al.'s adversary solver depends on). Tang, Ding, Sun, Gong (1,
  2025) separately show plain Equihash's own memory footprint has further
  give via LIR, independent of the regularity question. Dinur & Nadler (7,
  2017) and Blocki & Smearsoll (2, 2025) run the same arc one level over,
  for Biryukov & Khovratovich's *other* memory-hard PoW proposal (MTP)
  rather than Equihash. Ducas, Engelberts, Loyer (4, 2025) is the outlier
  — same algorithm (Wagner's), unrelated question (post-quantum lattice
  attack surface, not memory-hardness).
- **Received-date order and eprint-number order are not the same thing**:
  for the 2025-dated entries (1-4), this document sorts by each entry's own
  "Received:" date above, not by eprint number — eprint numbers are
  assigned sequentially through a year but don't reliably track
  received-date order across entries spanning a full year. Pre-2025
  entries (5-9) are ordered by publication year, since eprint received
  dates either don't apply (conference papers) or weren't the deciding
  factor at that resolution.
