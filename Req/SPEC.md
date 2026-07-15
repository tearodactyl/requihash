# SPEC.md — Equihash-family PoW specification

The family is one parameterized object:

    PoW(n, k, hash, m, keying, context)

Every element is a design freedom of this program. This document is the byte-exact
definition: where the tree implements a configuration point, the spec matches the
implementation (the cross-validated C++/Rust pair is authoritative for those
points); other points are specified here first and implemented against this text.
Chain-side packaging (Zebro era fields) consumes this spec and adds nothing to it.

## 1. Status of configuration points

| Configuration | Status |
|---|---|
| `hash=blake2b, m=1, keying=regular` (Requihash) | **Implemented**, cross-validated C++/Rust, vectors in `vectors/` |
| `hash=blake2b, m=1, keying=single` (Equihash-style) | Specified (§4.3); not implemented in this tree |
| `hash=blake2b, m≥2` | Specified (§5); not implemented |
| `hash=blake3` (any) | Specified (§6); not implemented |
| Compact solution encoding | Sized (§8.2); codec not implemented |
| `encoding` axis (leaf-string byte serialization, separate from `keying`) | **Proposed, not adopted** (§10) — `binary_le32` (current, implicit) vs. `ascii_decimal` (matches the paper's own Python Sequihash reference) |

## 2. Parameters and validity

- `n`: bit width; `n % 8 == 0`, `n % (k+1) == 0`, `k < n`.
- `k`: tree depth; a solution has `2^k` indices.
- `ℓ = n/(k+1)`: collision bit length. `cbyte = ⌈ℓ/8⌉`.
- Leaf index space: `i ∈ [0, 2^(ℓ+1))`.
- `hash ∈ {blake2b, blake3}`.
- `m ≥ 1`: generator iteration count. Consensus-side verification costs `2^k · m`
  hash units; deployments bound `m` by a verification budget.
- `keying ∈ {regular, single}`: `regular` is the Tang–Sun–Gong repair (leaves
  partitioned into `k` classes, restoring the k-list problem); `single` is
  classic single-list Equihash keying.
- `context`: the domain-separation stem. This tree uses the 10-byte ASCII stem
  `"ReqhashPoW"`. (Zcash's deployed Equihash uses a different personalization
  layout — 8-byte stem, le32(n), le32(k) — and is *not* bit-compatible with this
  family; compatibility mode is out of scope for v1.)

## 3. Generator, `hash = blake2b`

**Digest length.** `hash_output = ⌊512/n⌋ · n/8` bytes (the Equihash digest-length
convention). Only the first `n/8` bytes are consensus-relevant (§4); the tail is
a design freedom flagged for review (a future revision may set digest length to
`n/8`).

**Personalization** (BLAKE2b 16-byte personal field):

    person[0..10] = context stem ("ReqhashPoW")
    person[10..14] = le32(n)
    person[14..16] = le16(k)

**Base state** (shared by all leaves of one attempt):

    S0 = blake2b_init(digest_len = hash_output, person)
    S0.update(input)      // e.g. serialized header prefix
    S0.update(nonce)

For `m ≥ 2` the base state additionally absorbs `le16(m)` after the nonce
(§5); at `m = 1` nothing is absorbed, keeping `m = 1` bit-identical to the
implemented construction.

## 4. Leaf strings

### 4.1 `keying = regular` (implemented)

For leaf index `i`: `class = i mod k`, `counter = i div k`.

    D(i) = finalize( clone(S0).update(le32(class)).update(le32(counter)) )

One hash call per leaf; `2^(ℓ+1)` calls per attempt.

### 4.2 Leaf string and expanded row

The **leaf string** is the first `n` bits of `D(i)` (bytes `D[0 .. n/8]`).
For collision processing it is expanded to the padded row representation:
each ℓ-bit segment is placed big-endian into `cbyte` bytes with leading zero
padding (`expand_array` with `bit_len = ℓ`, `byte_pad = 0`; identical to the
zcashd `ExpandArray`). Byte comparison of a `cbyte` segment is then exactly
ℓ-bit comparison. The expanded row is representation, not consensus; the
consensus object is the n-bit leaf string.

### 4.3 `keying = single` (specified)

    D(i) = finalize( clone(S0).update(le32(i)) )

One `le32` word; no class word. (Note this is still one call *per leaf* —
deliberately simpler than Zcash Equihash's `⌊512/n⌋`-leaves-per-call packing.
Per-call packing in single mode is a permitted future optimization only if
specified as a new configuration point; it changes recomputation granularity
and therefore TMTO accounting.)

## 5. Iterated generator, `m ≥ 2` (specified)

Iteration applies to the whole digest, re-binding the leaf's keying words each
round so chains starting from distinct leaves cannot merge:

    D_1(i) = D(i)                     // §4, with S0 having absorbed le16(m)
    D_t(i) = finalize( blake2b_init(hash_output, person)
                       .update(D_{t-1}(i))
                       .update(keying words of i) )     for t = 2..m

Leaf string = first `n` bits of `D_m(i)`. The honest solver pays `m` calls per
leaf once; a trade-off adversary pays `m` per *recomputation* — the point of
the dial. The midstate optimization applies to `t = 1` only; that asymmetry is
intended.

## 6. Generator, `hash = blake3` (specified)

**Domain separation** via `derive_key` mode; the context string is ASCII:

    "ReqPoW/blake3 v1 ctx=<stem> n=<n> k=<k> m=<m> keying=<r|s>"

**Base:** `B0 = blake3::Hasher::new_derive_key(context)`; `B0.update(input)`;
`B0.update(nonce)`.

**Leaf strings via seekable XOF.** `regular`: one stream per class —
`B_c = clone(B0).update(le32(class))`; leaf `(class, counter)` string = XOF
output bytes `[counter · n/8, (counter+1) · n/8)` of `B_c`. `single`: one
stream, leaf `i` = bytes `[i · n/8, (i+1) · n/8)`. BLAKE3's XOF is O(1)-seekable
(output block computable directly from the root chaining value), so per-leaf
recomputation cost is flat — required for clean TMTO accounting.

**Iteration** (`m ≥ 2`): for blake3 the atomic unit is the leaf (not the
digest): `D_1(i)` = the `n/8` XOF bytes above;
`D_t(i) = blake3_derive_key(context, D_{t-1}(i) ‖ keying words)` truncated to
`n/8` bytes. Leaf string = `D_m(i)`. (Unit difference from blake2b (§5) is
deliberate: iteration binds to each hash's natural output granularity, which is
what a recomputing adversary must repeat.)

## 7. Solution validity

A solution is `2^k` leaf indices `i_0 .. i_{2^k−1}` such that, building a binary
tree over their expanded rows:

1. **Collision per round:** at round `r` (1-based), each adjacent pair XORs to
   zero on segment `r` (bytes `[(r−1)·cbyte, r·cbyte)` of the expanded rows —
   i.e. the r-th ℓ-bit segment).
2. **Algorithm binding (ordering):** at every internal node, the left child's
   index list is lexicographically less than the right child's.
3. **Distinctness:** all `2^k` indices are pairwise distinct.
4. **Zero root:** the XOR of all `2^k` expanded rows is zero in every segment.

Verification recomputes the `2^k` leaf strings (`2^k · m` hash units) and folds
the tree pairwise, checking 1–4; it holds no solver state. `keying = regular`
additionally implies each index's class is fixed by its position — the
regularity constraint is enforced by the leaf derivation itself (an index in
the wrong class simply hashes differently), not by an extra check.

## 8. Solution encodings

### 8.1 Minimal (implemented; Equihash-compatible shape)

`(ℓ+1)` bits per index, big-endian bit-packed (`compress_array`, identical to
zcashd): wire size `2^k · (ℓ+1) / 8` bytes — 1344 at (200,9). Round-trips via
`get_minimal_from_indices` / `get_indices_from_minimal`.

### 8.2 Compact (sized; codec not implemented)

`ℓ` bits per index — `2^k · ℓ / 8` bytes (1280 at (200,9)); the dropped bit per
index is reconstructed from the packet structure under regular keying (paper
Table 3). Codec definition lands with its implementation.

## 9. Vector file format (normative for cross-implementation and chain-side use)

One JSON object per file:

    { "n": u32, "k": u32,
      "input_hex": str, "nonce_hex": str,
      "minimal_hex": str, "indices": [u32],
      // optional, defaulting to the implemented configuration:
      "hash": "blake2b" | "blake3",   // default blake2b
      "m": u32,                        // default 1
      "keying": "regular" | "single"  // default regular
    }

A consumer must reject a vector whose optional fields name a configuration it
does not implement. This format is the only coupling surface to chain-side
benchmarking (a verifier consumes vectors; it never links this crate).

## 10. Proposed axis: `encoding` — separating the regularity binding from its byte serialization

**Status: proposal, not adopted.** Not yet a configuration point in §1's
table; recorded here because the family object (line 5 above) currently
conflates two independent decisions inside `keying`, and making them
separate axes turns three separately-implemented things (Equihash,
Requihash, and the paper's own Python Sequihash reference) into three named
points in one two-axis space instead.

### 10.1 The finding that motivates this

`Req/cpp/requihash.h`'s own comment (§4.1's implementation) already says
the relevant thing: *"any fixed leaf→class map that is non-constant across
the tree achieves the same effect"* as `i mod k`. The regularity
**binding** — draw leaf `i` from list-class `i mod k` — is the paper's
actual contribution (eprint 2025/1351 §5.2) and is what F-A4's ASIC-penalty
claim is about. How `(class, counter)` gets turned into hash-input bytes is
a separate, non-cryptographic decision, and the two existing
implementations made different arbitrary choices:

- **This repo (`keying=regular`, §4.1)**: `le32(class) || le32(counter)` —
  8 bytes, binary, fixed-width.
- **The paper's own Python reference**
  (`~/Work/ZK/ZKs/Generalized-Birthday-Problem/GBP-solver/k_list_algorithm.py`,
  `compute_item(i, j)`): `f"{i}-{j}".encode()` — a variable-length ASCII
  decimal string with a literal `-` separator, e.g. `b"3-42"`.

Checked directly: nothing in the paper's own artifact repo (code or
README-level text) mandates the string form as load-bearing — the paper's
contribution is stated in terms of the abstract binding (`x_i` from list
`i-1 mod K`), not a wire encoding. The string-vs-binary difference is very
likely just "two independent implementations, two independent quick
choices," not a deliberate design decision either side made for a reason —
recorded as a finding, not asserted as certain, since neither artifact's
authors were asked directly.

### 10.2 Proposed parametrization

Two independent axes, where today's `keying` conflates axis 1 with a fixed
choice on axis 2:

**Axis 1 — `keying`** (already exists, §1's table): `single` (no class
binding — classic Equihash) vs. `regular` (`i mod k` binding — Requihash).

**Axis 2 — `encoding`** (proposed, new): how the keying material for a
given leaf is turned into bytes before hashing.
- `binary_le32`: `le32(class) || le32(counter)` for `regular`; `le32(i)`
  for `single`. (This repo's current, only implemented choice.)
- `ascii_decimal`: `f"{class}-{counter}".encode()` for a `regular`-
  equivalent; `f"{i}".encode()` for a `single`-equivalent. (Matches the
  paper's own Python reference exactly.)

Naming choice: `encoding`, not `serialization` or `wire_format`, to keep it
visually and conceptually distinct from §8's "Solution encodings" (which is
about the *output* wire format, a completely different thing from this
axis, which is about the *input* to leaf hashing — worth a reader not
confusing the two just because both use the word "encoding").

### 10.3 Mapping to the three concrete instances

| Instance | `keying` | `encoding` | Notes |
|---|---|---|---|
| Equihash (Zcash, tromp, Khovratovich original) | `single` | `binary_le32` (or scheme-specific packing, e.g. Zcash's `⌊512/n⌋`-leaves-per-call — a third, deployment-specific encoding not modeled here, see §4.3's own note) | The historical baseline |
| Requihash (this repo) | `regular` | `binary_le32` | Current, only implemented point |
| Sequihash (paper's own Python reference) | `regular` | `ascii_decimal` | Same regularity binding as Requihash, different byte serialization — **not** a different scheme cryptographically, per 10.1 |

The paper's own name for its construction is "Sequihash" (`SIZING.md` §0);
this repo's "Requihash" and the paper's own "Sequihash" reference
implementation are therefore the *same point on axis 1* (`keying=regular`)
and differ only on axis 2 — a fact obscured by treating them as two
separately-named, separately-ported schemes rather than one point with two
encoding instantiations.

### 10.4 Security and complexity evaluation

**Security**: `encoding` should have **no effect on the security argument**
at all, and this is itself worth stating as a testable claim, not just an
assumption. The regularity binding (axis 1) is what F-A4's steepness claim
and H1-H8's shortcut-hunt (`SECURITY_ANALYSIS.md`) are about; nothing in
that analysis depends on *how* the class/counter pair is serialized, only
on *that* a leaf's class is fixed and non-constant across the tree. A
class-boundary-respecting attack (H1, class-selective TMTO) would transfer
identically regardless of `encoding`, since it operates on the abstract
list-class structure, not the byte layout. This is falsifiable: if a future
finding showed `encoding` *did* matter to security, that would itself be a
significant and surprising result worth its own write-up, not something
this proposal should assume away without flagging the possibility.

**Complexity**: real, but small and asymmetric.
- `binary_le32` is fixed-width (8 bytes for `regular`, 4 for `single`) —
  cheap to hash, cheap to reason about, no allocation beyond the fixed
  buffer already used today.
- `ascii_decimal` is variable-width (grows with the decimal digit-count of
  `class`/`counter`, which grows with `k`/`n`) — a real, if small,
  per-leaf cost difference (variable-length formatting plus a heap
  allocation for the formatted string in a naive implementation,
  vs. `binary_le32`'s fixed stack buffer). This is a legitimate, measurable
  performance difference between the two encodings, independent of the
  security-neutrality claim above — worth stating so "encoding doesn't
  affect security" isn't read as "encoding doesn't matter at all."
- Implementation cost of adding the axis: small. `GenerateHash`/`leaf_row`
  (the two places `keying=regular`'s binary encoding is currently
  hardcoded, `Req/cpp/requihash.h` and `rust/src/lib.rs`) would each need
  an `encoding` parameter threaded through, with `ascii_decimal` as a
  second branch — a config-surface change, not an algorithmic one.

### 10.5 Personalization/context as a related, third axis

`context` (line 5's family object; §3's `person` field) is already a real,
existing configuration point, and is worth naming explicitly alongside
`keying`/`encoding` since all three answer a similar question ("how is a
scheme distinguished from its neighbors") at different layers:

- **`context`**: distinguishes *this scheme family* from others (this
  repo's `"ReqhashPoW"` stem vs. Zcash's `"ZcashPoW"`-based layout, §3) —
  already flagged as "out of scope for v1 compatibility" (§3's own note).
- **`keying`**: distinguishes *regularity binding* (whether a leaf's class
  is fixed at all).
- **`encoding`** (this proposal): distinguishes *byte serialization* of
  whatever `keying` produces.

The paper's own Python reference has no `context`/personalization field at
all — its 16-byte "nonce" plays the combined role of this repo's
`input || nonce || person` prefix collapsed into one opaque value, with no
domain separation baked in between different `(n,k)` parameter sets or
scheme names. This is a fourth, separate finding worth recording precisely
for any future implementation claiming compatibility with the Python
reference: matching `encoding=ascii_decimal` alone is not sufficient for
byte-exact compatibility unless the `context`/prefix construction is also
matched — a real implementation must decide whether to reproduce the
Python reference's collapsed single-nonce-field design or keep this repo's
separated `input`/`nonce`/`person` structure and only match `encoding`, and
must state which choice it made.

### 10.6 What this proposal does not do

Does not promote `encoding` to §1's status table (that requires actual
implementation, or at minimum an accepted decision to implement it later).
Does not change any of Group D's task specs (`Req/PLAN.md` D1-D3) — RK is
unaffected (Khovratovich's original has its own independent leaf
construction, unrelated to either encoding); RT is unaffected (tromp's
index-pointer *algorithm* is orthogonal to leaf-string encoding entirely);
CS's task as specified (a standalone C++ port matching the Python
reference) remains the right execution vehicle for validating this
proposal's `10.1`/`10.3` claims empirically, once built — CS's own output
is exactly the artifact that would let this repo's engine (with `encoding`
added per this proposal) be cross-validated against an independent,
non-Req implementation of `ascii_decimal` mode, closing the loop between
this design proposal and Group D's execution work.
