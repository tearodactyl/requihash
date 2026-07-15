# UNIHASH.md — a unifying parametrization across Equihash, Requihash, and Sequihash

This document proposes a two-axis (plus a related third-axis) parametrization
that reframes Equihash, Requihash, and the 2025/1351 paper's own "Sequihash"
Python reference as three named points in one design space, rather than three
separately-implemented, separately-ported schemes. **Status: proposal, not
adopted anywhere.** Nothing here is implemented; `Req/SPEC.md` remains the
normative spec for what this project's own engine actually does
(`keying ∈ {regular, single}`, both already real configuration points there).
This document exists separately from `Req/SPEC.md` specifically so this
research/reframing work does not pollute the context of presently pending
implementation tasks (`Req/PLAN.md` Groups A-D) — read this when the question
is "how do these schemes relate," not when implementing or validating any of
the pending work.

## 1. The finding that motivates this

`Req/cpp/requihash.h`'s own comment (documenting `keying=regular`'s
implementation) already says the relevant thing: *"any fixed leaf→class map
that is non-constant across the tree achieves the same effect"* as `i mod k`.
The regularity **binding** — draw leaf `i` from list-class `i mod k` — is the
paper's actual contribution (Tang, Sun, Gong, "On the Regularity of the
Generalized Birthday Problem," [eprint 2025/1351](https://eprint.iacr.org/2025/1351)
§5.2) and is what `Req/SECURITY_ANALYSIS.md` F-A4's ASIC-penalty claim is
about. How `(class, counter)` gets turned into hash-input bytes is a
separate, non-cryptographic decision, and the two existing implementations
made different arbitrary choices:

- **`Req/` (this project's `keying=regular`)**: `le32(class) || le32(counter)`
  — 8 bytes, binary, fixed-width.
- **The paper's own Python reference**
  ([`tl2cents/Generalized-Birthday-Problem`](https://github.com/tl2cents/Generalized-Birthday-Problem),
  `GBP-solver/k_list_algorithm.py`, `compute_item(i, j)`; local clone
  `~/Work/ZK/ZKs/Generalized-Birthday-Problem`): `f"{i}-{j}".encode()` — a
  variable-length ASCII decimal string with a literal `-` separator, e.g.
  `b"3-42"`.

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
  paper's own Python reference exactly.)

Naming choice: `encoding`, not `serialization` or `wire_format`, to keep it
visually and conceptually distinct from `Req/SPEC.md` §8's "Solution
encodings" (which is about the *output* wire format — a different thing from
this axis, which is about the *input* to leaf hashing).

## 3. Mapping to the three concrete instances

| Instance | `keying` | `encoding` | Notes |
|---|---|---|---|
| Equihash (Zcash, tromp, Khovratovich's original) | `single` | `binary_le32` (or scheme-specific packing, e.g. Zcash's `⌊512/n⌋`-leaves-per-call — a third, deployment-specific encoding not modeled here, see `Req/SPEC.md` §4.3) | The historical baseline |
| Requihash (`Req/`, this project) | `regular` | `binary_le32` | Current, only implemented point |
| Sequihash (paper's own Python reference) | `regular` | `ascii_decimal` | Same regularity binding as Requihash, different byte serialization — **not** a different scheme cryptographically, per §1 above |

The paper's own name for its construction is "Sequihash" (`Req/SIZING.md`
§0); this project's "Requihash" and the paper's own "Sequihash" reference
implementation are therefore the *same point on axis 1* (`keying=regular`)
and differ only on axis 2 — a fact obscured by treating them as two
separately-named, separately-ported schemes rather than one point with two
encoding instantiations.

## 4. Security and complexity evaluation

**Security**: `encoding` should have **no effect on the security argument**
at all, and this is itself worth stating as a testable claim, not just an
assumption. The regularity binding (axis 1) is what F-A4's steepness claim
and H1-H8's shortcut-hunt (`Req/SECURITY_ANALYSIS.md`) are about; nothing in
that analysis depends on *how* the class/counter pair is serialized, only on
*that* a leaf's class is fixed and non-constant across the tree. A
class-boundary-respecting attack (H1, class-selective TMTO) would transfer
identically regardless of `encoding`, since it operates on the abstract
list-class structure, not the byte layout. This is falsifiable: if a future
finding showed `encoding` *did* matter to security, that would itself be a
significant and surprising result worth its own write-up, not something this
proposal should assume away without flagging the possibility.

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
- Implementation cost of adding the axis to `Req/`'s own engine, if ever
  adopted: small. `GenerateHash`/`leaf_row` (the two places
  `keying=regular`'s binary encoding is currently hardcoded, in
  `Req/cpp/requihash.h` and `Req/rust/src/lib.rs`) would each need an
  `encoding` parameter threaded through, with `ascii_decimal` as a second
  branch — a config-surface change, not an algorithmic one.

## 5. Personalization/context as a related, third axis

`context` (`Req/SPEC.md`'s family-object line; §3's `person` field) is
already a real, existing configuration point in `Req/`, and is worth naming
explicitly alongside `keying`/`encoding` since all three answer a similar
question ("how is a scheme distinguished from its neighbors") at different
layers:

- **`context`**: distinguishes *this scheme family* from others (`Req/`'s
  `"ReqhashPoW"` stem vs. Zcash's `"ZcashPoW"`-based layout, `Req/SPEC.md`
  §3) — already flagged there as "out of scope for v1 compatibility."
- **`keying`**: distinguishes *regularity binding* (whether a leaf's class
  is fixed at all).
- **`encoding`** (this proposal): distinguishes *byte serialization* of
  whatever `keying` produces.

The paper's own Python reference has no `context`/personalization field at
all — its 16-byte "nonce" plays the combined role of `Req/`'s
`input || nonce || person` prefix collapsed into one opaque value, with no
domain separation baked in between different `(n,k)` parameter sets or
scheme names. This is a fourth, separate finding worth recording precisely
for any future implementation claiming compatibility with the Python
reference: matching `encoding=ascii_decimal` alone is not sufficient for
byte-exact compatibility unless the `context`/prefix construction is also
matched — a real implementation must decide whether to reproduce the Python
reference's collapsed single-nonce-field design or keep `Req/`'s separated
`input`/`nonce`/`person` structure and only match `encoding`, and must state
which choice it made.

## 6. What this proposal does not do

Does not promote `encoding` to `Req/SPEC.md` §1's status table (that
requires actual implementation, or at minimum an accepted decision to
implement it later, at which point the relevant content should move back
into `Req/SPEC.md` as a real configuration point — this document is a
holding area for the idea, not a permanent second home for it). Does not
change any of `SOLVER_CORPUS.md`'s RK/RT/CS task specs — RK is unaffected
(Khovratovich's original has its own independent leaf construction, unrelated
to either encoding); RT is unaffected (tromp's index-pointer *algorithm* is
orthogonal to leaf-string encoding entirely); CS's task as specified (a
standalone C++ port matching the Python reference) remains the right
execution vehicle for validating this proposal's §1/§3 claims empirically,
once built — CS's own output is exactly the artifact that would let `Req/`'s
engine (with `encoding` added per this proposal) be cross-validated against
an independent, non-Req implementation of `ascii_decimal` mode, closing the
loop between this design proposal and the solver-corpus execution work.

## 7. References

- Biryukov, Khovratovich, "Equihash: Asymmetric Proof-of-Work Based on the
  Generalized Birthday Problem," NDSS 2016 —
  [paper](https://www.internetsociety.org/sites/default/files/blogs-media/equihash-asymmetric-proof-of-work-based-generalized-birthday-problem.pdf),
  [reference implementation](https://github.com/khovratovich/equihash).
- Tang, Sun, Gong, "On the Regularity of the Generalized Birthday Problem,"
  eprint 2025/1351 — [paper](https://eprint.iacr.org/2025/1351),
  [artifact repo](https://github.com/tl2cents/Generalized-Birthday-Problem)
  (local clone `~/Work/ZK/ZKs/Generalized-Birthday-Problem`).
- `zcash/zcash`, `src/crypto/equihash.{h,cpp,tcc}` — local clone
  `~/Work/ZK/ZKs/zcash`.
- `Req/SPEC.md`, `Req/cpp/requihash.h`, `Req/rust/src/lib.rs` — this
  project's own implementation, the basis for axis 1/2's "current
  implemented point" claims throughout.
