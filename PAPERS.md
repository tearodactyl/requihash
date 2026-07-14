# PAPERS.md — 2025 papers relevant to this project's topics

Four papers, obtained as PDFs (`~/Downloads/2025-575.pdf`, `2025-1351.pdf`,
`2025-1456.pdf`, `2025-2141.pdf`) and scanned directly (not from secondary
summaries) on 2026-07-14. Titles, authors, and venue status below are
transcribed from each PDF's own title page and/or the eprint listing
metadata, checked live against `eprint.iacr.org`. Two of the four are by
overlapping authors and are the direct source of this project's "Requihash"
construction and its published parameter table (SIZING.md).

## 1. On the Regularity of the Generalized Birthday Problem

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

**A second, closely related paper by three of the same four authors** (see
paper 4 below) revisits the same memory-hardness question from an
implementation-optimization angle rather than a theory angle.

## 2. Wagner's Algorithm Provably Runs in Subexponential Time for SIS∞

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
and 4 address, but the same underlying algorithm.

## 3. Provably Memory-Hard Proofs of Work With Memory-Easy Verification

**Authors:** Jeremiah Blocki, Nathan Smearsoll (Purdue University, West
Lafayette, IN).
**eprint:** [2025/1456](https://eprint.iacr.org/2025/1456). Status: eprint
listing only as of this check.

Revisits Biryukov and Khovratovich's Merkle Tree Proofs (MTP) memory-hard
proof-of-work candidate (USENIX 2016), built on the Argon2d memory-hard
function — a construction Dinur and Nadler (CRYPTO 2017) broke, exploiting
data-dependencies in the underlying Argon2d graph. This paper formally
proves, in the parallel random oracle model, that the MTP framework *is*
sound when instantiated with a suitably *data-independent* memory-hard
function, generically lower-bounding any prover's cumulative memory cost by
the pebbling cost of the construction's "ex-post-facto" graph. When
instantiated with DRSample, the resulting scheme has an honest prover running
in sequential time O(N), proof size and verification time polylog(N), and any
malicious prover forced to Ω(N²/log N) cumulative memory complexity. Also
develops general pebbling attacks showing any iMHF-based MHPoW using the MTP
framework has proof size at least Ω(log²N/log log N), and at least
Õ(N^0.32) when instantiated with Argon2i specifically. Relevant to this
project's broader interest in provably-secure (not just conjectured-secure)
memory-hard constructions, as a contrast case to Equihash/Sequihash's
security argument, which (per paper 1) rested on an unproven — and, per that
paper, incorrect — problem-equivalence assumption for two decades.

## 4. Memory Optimizations of Wagner's Algorithm with Applications to Equihash

**Authors:** Lili Tang, Rui Ding, Yao Sun, Xiaorui Gong (same two
institutions as paper 1; Rui Ding added as a fourth author, not present on
paper 1).
**eprint:** [2025/2141](https://eprint.iacr.org/2025/2141). Status: eprint
listing only as of this check.
**Artifact repo:** [tl2cents/Wagner-Algorithms](https://github.com/tl2cents/Wagner-Algorithms)
("Memory Optimizations of single-list Wagner's Algorithm"; created
2025-10-04, last pushed 2026-02-21) — **a second, separate repository from
paper 1's**, not yet cloned or run by this project.

A practically-oriented companion to paper 1, focused specifically on List
Item Reduction (LIR) techniques — reducing the amortized cost of storing
indices and hash values per list item, as opposed to List Size Reduction
(LSR, reducing the list size itself, which the paper shows incurs
prohibitive time penalties for Equihash specifically — e.g. halving
Equihash(200,9)'s memory via the state-of-the-art LSR trade-off costs a time
penalty factor of 2^24.6). Introduces a hybrid framework achieving,
per the abstract, "more than 50%" peak memory reduction (from >2nN to nN
bits) at roughly a twofold time penalty, across all Equihash parameters.
**Concrete, directly-quoted implementation numbers, distinct from paper 1's
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

## Cross-paper notes

- Papers 1 and 4 share three authors (Tang, Sun, Gong) and are best read as a
  matched theory/practice pair: paper 1 established that Equihash's
  index-pointer defeat rests on a mis-specified problem and proposed the
  regularity fix (Sequihash/Requihash); paper 4 shows, independently of that
  fix, how much of the *original* Equihash's own memory footprint can still
  be optimized away with list-item-reduction techniques the field hadn't
  systematically explored before. Both bear directly on the D3 evidence
  question this project is building toward: paper 4's numbers say Equihash's
  memory-hardness has more give in *both* directions than assumed — attacker
  optimizations already exist (index pointers, and now LIR) and so does a
  principled repair (Requihash/Sequihash), and neither this project nor (as
  far as verified) anyone else has yet run paper 4's actual code against
  paper 1's Requihash construction to see how LIR-style optimizations would
  affect Requihash's claimed 2.3–4.9× memory penalty over plain Equihash.
- Papers 2 and 3 are not by the Sequihash authors and address different
  questions (lattice cryptanalysis; a competing memory-hard PoW framework's
  own provable-security gap) but were bundled with this reading pass because
  they're 2025-dated, adjacent-topic (Wagner's algorithm; memory-hard PoW
  provable security) results worth having cited in one place.
- None of the four papers has a confirmed peer-reviewed venue acceptance as
  of this check (2026-07-14) — all four are eprint-only listings, though
  paper 1's acknowledgments indicate at least one round of review at a major
  venue.
