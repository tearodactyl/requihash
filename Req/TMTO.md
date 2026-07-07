# TMTO.md — Time-memory tradeoff: 2017 research and implications for Req

The security of a memory-hard proof-of-work is not "it needs M bytes" — a solver
can always use less memory and recompute. What matters is the *time-memory
tradeoff* (TMTO): how steeply the computation cost rises when memory is cut. This
note collects the 2017 TMTO results that define the standard Equihash is judged
against, and works out what they imply for this implementation. Findings context:
[../Equihash.md](../Equihash.md) F-A3, F-A4, F-A6.

## 1. The 2017 TMTO results

### 1a. Equihash's own tradeoff claim (Biryukov-Khovratovich, Ledger 2017)

The [Equihash paper](https://ledger.pitt.edu/ojs/ledger/article/view/48) states its
security as *steepness*: the TMTO for algorithm A has steepness `s` if reducing
memory by a factor `q` raises computation by roughly `q^s`. Higher steepness is
better (a memory-cutting ASIC pays more). The paper's propositions, verbatim in
form:

- **Prop 4 (plain Wagner).** With `M/q` memory, finding a `2^k`-XOR solution costs
  `C1(q) ≈ (3q^{(k-1)/2} + k)/(k+1)` times more — **steepness (k-1)/2**.
- **Prop 6 (algorithm-bound).** With `M/q` memory, the algorithm-bound problem
  costs `C2(q) ≈ 2^k · q^{k/2} · k^{k/2-1}` — **steepness k/2**, strictly higher.
  This higher steepness is exactly what "algorithm binding" buys, and it is the
  security argument for Equihash's tradeoff resistance.
- The out-of-memory (Bernstein) tradeoff gives penalty `C(q) = q^k` in the naive
  form, improved to `C(q) ≈ 4·2^{n/(k+1)}·q^{(k+1)/2}` with memoryless collision
  search at the last step.

At (200,9), k=9, so algorithm-bound steepness is `k/2 = 4.5`: cutting memory 2x
should cost ~`2^4.5 ≈ 23x` more computation, and the paper's headline example is
that a 250 MB-limited adversary pays ~1000x, a memoryless one ~2^75 hash calls.

### 1b. The refutation (Alcock-Ren, CCSW 2017)

[Alcock and Ren](https://dl.acm.org/doi/10.1145/3140649.3140652) showed the
steepness argument rests on the same regular-vs-loose confusion that the 2025
paper later formalized: Equihash's propositions analyze the *regular* k-list
problem, but Equihash solves the *loose single-list* problem, and **no
tradeoff-resistance bound is actually proven for the problem Equihash uses**. The
steepness `k/2` is a claim about a different problem. This is the TMTO-specific
face of Equihash.md F-A2.

### 1c. The cautionary attack (Dinur-Nadler, CRYPTO 2017)

[Dinur and Nadler](https://eprint.iacr.org/2017/497) devised a **sub-linear
computation-memory tradeoff** against MTP (the Biryukov-Khovratovich sibling PoW),
breaking its claim that low-memory provers suffer large penalties. MTP is not
Equihash, but the lesson generalizes and is the load-bearing one for any
implementation: **a memory-hard PoW's steepness claim can be false against an
unforeseen tradeoff, and is only as strong as the best known attack** — until the
TCC 2025 pebbling lower bounds (Equihash.md F-A6), no steepness claim in this
family was a theorem.

## 2. What this implies for Req

The 2017 TMTO literature is not just history for this implementation — it defines
what the solver backends *are*, and what the Requihash repair must be tested for.

**Implication 1: the M/q regime is a solver backend, and we can measure its
steepness.** Prop 4/6 describe solvers that use `M/q` memory and pay a time
penalty. Our arena and bucket solvers are the `q=1` (full-memory) point; a
Bernstein-style truncating solver is the `q>1` point. The implementation can
therefore *measure* the empirical steepness — mine at full memory, then at half
and quarter memory via hash truncation, and fit the penalty curve — rather than
trust the formula. This is the single most valuable TMTO experiment the codebase
enables, and nothing in the reference solvers or the paper reports it for the
loose problem the refutation flagged.

**Implication 2: Requihash's whole value proposition is a steepness claim, and it
is testable here.** F-A4's "12x ASIC memory penalty" is the k/2-steepness argument
applied to the *regular* problem that Requihash restores — the argument Equihash
could not legitimately make (1b) but Requihash can, because it actually solves the
regular k-list problem the propositions analyze. The regularity constraint moves
the solver from the Prop-4/loose regime to the Prop-6/regular regime. Our
`solve::bucket` backend is a regular-problem solver; adding a memory-reduced
variant and measuring whether the penalty tracks `q^{k/2}` would be the first
empirical check of the Requihash steepness claim, on the exact problem the claim
is about.

**Implication 3: index-pointer storage is the memory axis of the tradeoff.** The
(2^k)/k space win (OPTIMIZATION_HISTORY.md technique 1, not yet ported) is a *move
along the TMTO curve* — it reduces memory without recomputation, so it is the
benign end of the tradeoff. Its interaction with the malign end (Bernstein
truncation) is what sets the real steepness. A production solver needs both to
sit at the honest operating point, and to measure an attacker's, on the same
memory axis.

**Implication 4: don't trust our own steepness — test it.** The Dinur-Nadler
lesson (1c) applied to this codebase: the arena/bucket solvers should not be
assumed tradeoff-optimal. The bucket count, the static allocation, and the
incomplete sort all fix the memory-access pattern (which is what made the 2016-17
Equihash solvers ASIC-friendly); for Requihash that is safe (F-A4), but the
implementation should carry a TMTO test harness that tries to *beat* its own
memory floor, the way Dinur-Nadler beat MTP's. A steepness claim with no attack
attempt behind it is exactly the posture 2017 punished.

## 3. Proposed TMTO experiments (next round)

Concrete, ordered by value:

1. **Empirical steepness of the regular solver.** Add a memory-capped solve
   (Bernstein truncation: hash to `n - (k+1)log q` bits, repeat `q^{k+1}` times)
   as a solver backend; measure computation vs `q` at (96,5)/(144,5); fit `s` and
   compare to the predicted `k/2`. Deliverable: a steepness curve, the first for
   the regular Requihash problem.
2. **Equihash vs Requihash steepness side-by-side.** Run the same memory-capped
   solve against single-list (Equihash) and regularity-constrained (Requihash)
   leaf keying; confirm Requihash's curve is steeper — the empirical form of the
   12x-penalty claim (F-A4).
3. **Adversarial pass.** Attempt a sub-linear tradeoff against the Requihash
   solver (the Dinur-Nadler posture); a failure to find one is weak evidence for
   the claim, a success is a finding that would matter to every Requihash adopter.
4. **Pebbling cross-check.** Relate the measured curve to the TCC 2025 pebbling
   lower bound (F-A6) — the theorem the empirical steepness should not be able to
   beat.

## 4. Summary

| 2017 result | What it established | Implication for Req |
|---|---|---|
| Equihash Prop 4/6 (steepness (k-1)/2 vs k/2) | Algorithm binding raises TMTO steepness | The M/q regime is a measurable solver backend; k/2 is the target curve |
| Alcock-Ren refutation | No tradeoff bound proven for Equihash's *loose* problem | Requihash's regular problem is the one the k/2 claim legitimately applies to |
| Dinur-Nadler MTP attack | Steepness claims fall to unforeseen tradeoffs | Ship a TMTO test harness that attacks our own memory floor |
| TCC 2025 pebbling (context) | First provable memory lower bounds | The ceiling the measured steepness must respect |

The through-line: **Equihash's TMTO security was a claim about the wrong problem,
Requihash makes it a claim about the right one, and this implementation is the
place to test whether the claim holds empirically** — which is the experiment the
whole 2017 literature says should have been run before deployment.
