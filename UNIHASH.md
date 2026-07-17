# UNIHASH.md — a unifying parametrization across Equihash, Requihash, and Sequihash

**Status: proposal, not adopted anywhere.** Nothing here is implemented;
`Req/SPEC.md` remains the normative spec for what this project's own
engine actually does (`keying ∈ {regular, single}`, both already real
configuration points there — `regular` implemented and cross-validated,
`single` specified only). This document exists separately from
`Req/SPEC.md` so this research/reframing work does not pollute the context
of presently pending implementation tasks (`Req/PLAN.md` Groups A-D); read
this when the question is "how do these schemes relate," not when
implementing or validating any of the pending work.

## 0. Introduction: three names, two schemes, one open question

Three names circulate for closely related generalized-birthday-problem
proof-of-work constructions — **Equihash** (Biryukov & Khovratovich 2016),
**Requihash** (this project's own name for its implementation of the
regularity repair), and **Sequihash** (the term the repair's own source
paper uses for the identical construction, Tang, Sun, Gong 2025 §5.2,
`PAPERS.md` §3). The natural first assumption — three names, three
schemes — is wrong for two of them: Requihash and Sequihash are the *same*
regularity-repaired construction, verified directly against both source
implementations (§1 below), differing only in a byte-serialization choice
neither paper nor project treats as load-bearing. Equihash genuinely is a
different scheme (no regularity binding at all), but even that difference
reduces to flipping one configuration axis, not redesigning the algorithm.

This document's actual claim is narrow and falsifiable: that the space of
"Equihash-family" constructions this project has touched — classic
Equihash, this project's Requihash, and the paper's own Sequihash
reference — collapses to two independent axes (`keying`, whether leaf
generation binds a list-class at all; `encoding`, how the resulting keying
material becomes hash-input bytes) plus a third, already-real axis
(`context`/personalization) that answers a related but distinct question.
Stating this precisely, with exact provenance for each implementation's
actual choice, is the point — not proposing new cryptography.

## 1. The finding that motivates this

`Req/cpp/requihash.h`'s own comment (documenting `keying=regular`'s
implementation) already says the relevant thing: *"any fixed leaf→class map
that is non-constant across the tree achieves the same effect"* as `i mod k`.
The regularity **binding** — draw leaf `i` from list-class `i mod k` — is the
paper's actual contribution (Tang, Sun, Gong 2025 §5.2, `PAPERS.md` §3) and
is what `Req/SECURITY_ANALYSIS.md` F-A4's ASIC-penalty claim is about. How
`(class, counter)` gets turned into hash-input bytes is a separate,
non-cryptographic decision, and the two existing implementations made
different arbitrary choices — verified directly against both source files,
not inferred:

- **`Req/` (this project's `keying=regular`)**: `Req/cpp/requihash.h`'s
  `GenerateHash` (lines 190-203) constructs `le32(listclass) || le32(counter)`
  — 8 bytes, binary, fixed-width, appended to a base BLAKE2b state that has
  already absorbed a 16-byte personalization field plus `input || nonce`
  (full construction: §3 below).
- **The paper's own Python reference**
  ([`tl2cents/Generalized-Birthday-Problem`](https://github.com/tl2cents/Generalized-Birthday-Problem),
  `GBP-solver/k_list_algorithm.py`, `compute_item(i, j)` at line 107; local
  clone `~/Work/ZK/ZKs/Generalized-Birthday-Problem`), read verbatim:

  ```python
  def compute_item(self, i: int, j: int):
      message = self.nonce + f"{i}-{j}".encode()
      return int.from_bytes(self.hashfunc(message), 'big')
  ```

  i.e. `self.nonce || f"{i}-{j}".encode()` — the **16-byte nonce is
  concatenated as a raw prefix**, followed by a variable-length ASCII
  decimal string with a literal `-` separator (e.g. `b"3-42"`), and the
  whole concatenation is hashed in one call. (An earlier revision of this
  document's own §1 quoted only the `f"{i}-{j}".encode()` suffix and
  omitted the nonce prefix — corrected here after re-reading the source
  directly; the omission changed no conclusion below, since §3's nonce
  discussion already treated the nonce as prepended, but the quoted code
  itself was wrong and is fixed.)

`i` here is the list index (Requihash's `class`) and `j` the item index
within that list (Requihash's `counter`) — confirmed by `compute_item`'s
own docstring ("Compute the j-th item of the i-th list of messages") and
by how the constructor's `__init__` (lines 31-60) sets up exactly `2^lgk`
lists. So the *meaning* of `i`/`j` and `class`/`counter` match exactly;
only the byte serialization differs.

Checked directly: nothing in the paper's own artifact repo (code or
README-level text) mandates the string form as load-bearing — the paper's
contribution is stated in terms of the abstract binding (`x_i` from list
`i-1 mod K`), not a wire encoding. The string-vs-binary difference is very
likely just "two independent implementations, two independent quick
choices," not a deliberate design decision either side made for a reason —
recorded as a finding, not asserted as certain, since neither artifact's
authors were asked directly.

## 2. Proposed parametrization

Two independent axes, where `Req/SPEC.md`'s existing `keying` conflates axis 1
with a fixed choice on axis 2:

**Axis 1 — `keying`** (real, already implemented as a config point in
`Req/SPEC.md` §1/§2): `single` (no class binding — classic Equihash) vs.
`regular` (`i mod k` binding — Requihash).

**Axis 2 — `encoding`** (proposed, new, not implemented anywhere): how the
keying material for a given leaf is turned into bytes before hashing.
- `binary_le32`: `le32(class) || le32(counter)` for `regular`; `le32(i)` for
  `single`. (This project's current, only implemented choice, in `Req/`.)
- `ascii_decimal`: `f"{class}-{counter}".encode()` for a `regular`-
  equivalent; `f"{i}".encode()` for a `single`-equivalent. (Matches the
  paper's own Python reference exactly, once the nonce prefix from §1 is
  accounted for separately — `ascii_decimal` names only the keying-material
  serialization, not the full hash-input construction, which is §3's
  concern.)

Naming choice: `encoding`, not `serialization` or `wire_format`, to keep it
visually and conceptually distinct from `Req/SPEC.md` §8's "Solution
encodings" (which is about the *output* wire format — a different thing from
this axis, which is about the *input* to leaf hashing).

### 2a. `single` with `binary_le32` — provenance and implementation

This is `Req/SPEC.md` §4.3's own specified-but-unimplemented construction:
`D(i) = finalize(clone(S0).update(le32(i)))` — one `le32` word, no class
word, one hash call per leaf. It is explicitly *not* Zcash's actual deployed
Equihash construction, which packs `⌊512/n⌋` leaves per hash call
(`Req/SPEC.md` §4.3's own note); `single`/`binary_le32` as specified here is
this project's own minimal single-list construction, chosen for structural
symmetry with `regular`/`binary_le32` (same base-state/call-per-leaf shape,
differing only in whether a class word is appended), not a claim of
byte-compatibility with zcashd. No KAT vectors validate this construction
yet — the vectors pulled so far (`Req/PLAN.md` A14) are for zcashd's actual
per-call-packed construction, a third, different point not modeled by this
axis at all (`Req/SPEC.md` §4.3 flags this explicitly). Implementing
`single`/`binary_le32` in `Req/`'s own engine (`Req/PLAN.md` A19's
prerequisite) is unstarted.

### 2b. `regular` with `ascii_decimal` — provenance and implementation

This is the paper's own Python reference's exact construction, per §1
above: `self.nonce || f"{class}-{counter}".encode()`, hashed as one call
per leaf via `hashlib.blake2b(message, digest_size=n//8)` — plain BLAKE2b
with **no personalization field, no keyed mode, no salt** (confirmed
directly: `k_list_algorithm.py` line 26-27's `blake2b` wrapper passes only
`digest_size` to `hashlib.blake2b`). This is a materially simpler
construction than `Req/`'s own `regular`/`binary_le32` (§3 below) at the
base-state level, not just at the per-leaf encoding level — no domain
separation exists between different `(n,k)` runs or between this scheme
and any other hash use, only between different `nonce` values. Nobody has
implemented `regular`/`ascii_decimal` in `Req/`'s own engine; CS
(`Req/SOLVER_CORPUS.md`) is the standalone port targeting exactly this
construction, faithfully, including the ASCII formatting (no leading
zeros, no fixed width, literal `-` separator) — CS's own task spec already
states this as a hard requirement, independently arrived at before this
document's own analysis, and consistent with it.

## 3. Nonce, block content, and personalization — the base-state axis

`keying` and `encoding` (axes 1-2) both describe what happens *after* a
per-attempt base hash state exists. A third, largely-orthogonal question is
what goes *into* that base state before any leaf-specific material is
appended — and here the two implementations diverge more than axes 1-2
alone would suggest.

**`Req/`'s own construction** (`Req/cpp/requihash.h`'s `InitialiseState`,
lines 174-182; `Req/SPEC.md` §3, normative): a 16-byte BLAKE2b
personalization field (byte ranges half-open, `[start, end)`):
`person[0..6) = "ReqPoW"` (this project's own 6-byte ASCII domain-
separation stem, distinct from Zcash's `"ZcashPoW"`-based layout —
`Req/SPEC.md` §2's `context` note), `person[6..10)` reserved (zero),
`person[10..14) = le32(n)`, `person[14..16) = le16(k)` — so the personalization field itself
already binds the scheme name *and* the exact `(n,k)` parameter pair before
a single byte of block content is absorbed. The base state is then built as
`S0 = blake2b_init(digest_len, person)`, `S0.update(input)`,
`S0.update(nonce)` — `input` standing in for "serialized header prefix" in
a real chain deployment (`Req/SPEC.md` §3's own comment; the full
block-binding chain — how header fields, nonce, and difficulty interlock —
is `Req/SECURITY_ANALYSIS.md` §3's dedicated treatment, not repeated here).
For `m ≥ 2` (the iterated-generator axis, `Req/SPEC.md` §5, itself
unrelated to `keying`/`encoding`) the base state additionally absorbs
`le16(m)` after the nonce; at `m = 1` nothing is absorbed, so this
document's analysis is unaffected by the `m` axis at its default.

**The paper's own Python reference** has no personalization field, no
scheme-name stem, and no explicit `(n,k)` binding in the hash input at all
— its entire base-state role is played by the constructor's 16-byte
`nonce` argument (`k_list_algorithm.py` line 31-46; `os.urandom(16)` if not
supplied), concatenated raw as the message prefix in every `compute_item`
call. This means: two different `(n,k)` parameter choices run against the
*same* nonce would produce leaf hashes that differ only through `n`'s
effect on `hashfunc`'s output digest size (via `self.hashfunc = lambda x:
blake2b(x, n // 8)`) and through however the caller chooses `i,j`'s ranges
— there is no independent domain-separation tag distinguishing "this is a
`(96,5)` run" from "this is a `(200,9)` run" beyond the caller's own
external bookkeeping of which nonce was used for which parameter set. This
is a real, structural difference from `Req/`'s design, not just a smaller
version of the same idea.

**Consequence for any implementation claiming Python-reference
compatibility** (already noted once, restated here as this section's own
conclusion since it belongs with the full nonce/personalization analysis,
not as a late aside): matching `encoding=ascii_decimal` alone is not
sufficient for byte-exact compatibility with the Python reference unless
the base-state construction is also matched — a real implementation (CS,
concretely) must decide whether to reproduce the Python reference's
collapsed single-nonce-field design (no personalization, no separate
`input`) or keep `Req/`'s separated `input`/`nonce`/`person` structure and
only match the per-leaf `encoding`, and must state which choice it made.
CS's own task spec (`Req/SOLVER_CORPUS.md`) already commits to the former
— faithful reproduction of the Python reference as it actually is,
including its collapsed nonce-only base state — which this section's
analysis independently confirms is the correct reading of what "faithful"
requires here.

**Variation space, named for completeness:** at least three independent
personalization/base-state postures exist across implementations this
project has read directly — (a) `Req/`'s own: scheme-stem + `(n,k)` in a
16-byte BLAKE2b `person` field, `input`/`nonce` absorbed separately; (b)
zcashd's actual deployed Equihash: an 8-byte stem + `le32(n)` + `le32(k)`
personalization layout, structurally similar to (a) but not byte-compatible
with it (`Req/SPEC.md` §2's `context` note); (c) the paper's Python
reference: no personalization field, a single opaque 16-byte nonce
absorbing all domain-separation duty. None of the three is "more correct"
— they trade off differently between simplicity (c), byte-compatibility
with an existing deployed chain (b), and this project's own explicit
non-goal of Zcash compatibility while still wanting parameter-set domain
separation (a). A fourth point — no personalization at all, relying
entirely on `input` (the real header) for domain separation — is
theoretically possible but not implemented or specified by any source this
document has reviewed.

## 4. Mapping to the three concrete instances

| Instance | `keying` | `encoding` | Base-state posture | Notes |
|---|---|---|---|---|
| Equihash (Zcash, tromp, Khovratovich's original) | `single` | `binary_le32` (or scheme-specific packing, e.g. Zcash's `⌊512/n⌋`-leaves-per-call — a third, deployment-specific encoding not modeled here, see `Req/SPEC.md` §4.3) | zcashd: 8-byte stem + `le32(n)` + `le32(k)` (§3(b)) | The historical baseline |
| Requihash (`Req/`, this project) | `regular` | `binary_le32` | `Req/`'s own: 10-byte stem + `le32(n)` + `le16(k)`, separated `input`/`nonce` (§3(a)) | Current, only implemented point |
| Sequihash (paper's own Python reference) | `regular` | `ascii_decimal` | No personalization; single opaque nonce (§3(c)) | Same regularity binding as Requihash, different byte serialization *and* different base-state construction — **not** a different scheme cryptographically, per §1 above, but genuinely a different concrete byte-compatibility target because of §3, not just §2 |

The paper's own name for its construction is "Sequihash" (`Req/SIZING.md`
§0); this project's "Requihash" and the paper's own "Sequihash" reference
implementation are therefore the *same point on axis 1* (`keying=regular`)
and differ on both axis 2 (`encoding`) and the base-state axis (§3) — two
independent divergences, not one, a fact obscured by treating them as two
separately-named, separately-ported schemes rather than one algorithmic
point with two different concrete instantiations around it.

## 5. Security and complexity evaluation

**Security**: `encoding` and the base-state posture (§3) should have **no
effect on the security argument** at all, and this is itself worth stating
as a testable claim, not just an assumption. The regularity binding (axis
1) is what F-A4's steepness claim and H1-H8's shortcut-hunt
(`Req/SECURITY_ANALYSIS.md`) are about; nothing in that analysis depends on
*how* the class/counter pair is serialized or *what* domain-separation
scheme wraps it, only on *that* a leaf's class is fixed and non-constant
across the tree. A class-boundary-respecting attack (H1, class-selective
TMTO) would transfer identically regardless of `encoding` or base-state
posture, since it operates on the abstract list-class structure, not the
byte layout. This is falsifiable: if a future finding showed either axis
*did* matter to security, that would itself be a significant and
surprising result worth its own write-up, not something this proposal
should assume away without flagging the possibility.

**Complexity**: real, but small and asymmetric.
- `binary_le32` is fixed-width (8 bytes for `regular`, 4 for `single`) —
  cheap to hash, cheap to reason about, no allocation beyond a fixed buffer.
- `ascii_decimal` is variable-width (grows with the decimal digit-count of
  `class`/`counter`, which grows with `k`/`n`) — a real, if small, per-leaf
  cost difference (variable-length formatting plus a heap allocation for the
  formatted string in a naive implementation, vs. `binary_le32`'s fixed
  stack buffer). This is a legitimate, measurable performance difference
  between the two encodings, independent of the security-neutrality claim
  above — worth stating so "encoding doesn't affect security" isn't read as
  "encoding doesn't matter at all."
- The base-state posture (§3) has its own, separate complexity note:
  `Req/`'s personalized construction pays one extra `blake2b_init`-with-
  `person` cost once per attempt (not per leaf — the personalized state is
  cloned per leaf, `Req/cpp/requihash.h` line 193), so its marginal cost
  over the Python reference's no-personalization design is amortized to
  effectively zero at any real leaf count and is not a meaningful
  performance difference between the two, unlike the `encoding` axis above.
- Implementation cost of adding the `encoding` axis to `Req/`'s own engine,
  if ever adopted: small. `GenerateHash`/`leaf_row` (the two places
  `keying=regular`'s binary encoding is currently hardcoded, in
  `Req/cpp/requihash.h` and `Req/rust/src/lib.rs`) would each need an
  `encoding` parameter threaded through, with `ascii_decimal` as a second
  branch — a config-surface change, not an algorithmic one. Adding the
  base-state posture as a further axis would be a larger change (it
  touches `InitialiseState`/`S0` construction, not just per-leaf keying)
  and is not proposed here as a fourth formal axis — §3 documents it as
  found variation, not as a recommendation to generalize `Req/`'s own
  engine over it.

## 6. What this proposal does not do

Does not promote `encoding` (or the base-state posture of §3) to
`Req/SPEC.md` §1's status table (that requires actual implementation, or at
minimum an accepted decision to implement it later, at which point the
relevant content should move back into `Req/SPEC.md` as a real
configuration point — this document is a holding area for the idea, not a
permanent second home for it). Does not change any of `SOLVER_CORPUS.md`'s
RK/RT/CS task specs — RK is unaffected (Khovratovich's original has its own
independent leaf construction, unrelated to either encoding or the
base-state question here); RT is unaffected (tromp's index-pointer
*algorithm* is orthogonal to leaf-string encoding entirely); CS's task as
specified (a standalone C++ port matching the Python reference,
base-state posture included) remains the right execution vehicle for
validating this proposal's §1/§3/§4 claims empirically, once built — CS's
own output is exactly the artifact that would let `Req/`'s engine (with
`encoding` and the base-state posture both made configurable per this
proposal) be cross-validated against an independent, non-Req
implementation of the paper's exact construction, closing the loop between
this design proposal and the solver-corpus execution work.

## 7. Provenance summary

Every implementation-specific claim above was checked directly against
source, not carried from memory or a prior pass: `Req/cpp/requihash.h`
(`InitialiseState`, `GenerateHash`, the `person()` method) for `Req/`'s own
construction; `k_list_algorithm.py`'s `__init__` and `compute_item` for the
paper's Python reference; `Req/SPEC.md` §2-5 for the normative
specification of what `Req/`'s engine does and does not yet implement. Full
citations for the source papers named throughout (Biryukov & Khovratovich
2016; Tang, Sun, Gong 2025) are in `PAPERS.md` §8 and §3 respectively —
not repeated here, per the same single-source-of-truth convention
`Equihash.md` §9 now follows.
