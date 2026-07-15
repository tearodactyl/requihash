# SPEC.md — Equihash-family PoW specification

The family is one parameterized object:

    PoW(n, k, hash, m, keying, context)

Every element is a design freedom of this program, and none of the specific
choices below (digest-length convention, personalization layout, byte
encodings) is claimed to be the only correct or most proper way to
instantiate this family — they are this project's own choices, made
concrete so the C++/Rust pair can be byte-exact against each other, not
asserted as canonical against the wider design space (see `UNIHASH.md` for
where this family sits relative to sibling constructions). This document is
the byte-exact definition: where the tree implements a configuration point,
the spec matches the implementation (the cross-validated C++/Rust pair is
authoritative for those points); other points are specified here first and
implemented against this text. Chain-side packaging (Zebro era fields)
consumes this spec and adds nothing to it.

**Primary sources this spec is based on:**
- Biryukov, Khovratovich, "Equihash: Asymmetric Proof-of-Work Based on the
  Generalized Birthday Problem," NDSS 2016 —
  [paper](https://www.internetsociety.org/sites/default/files/blogs-media/equihash-asymmetric-proof-of-work-based-generalized-birthday-problem.pdf),
  [reference implementation](https://github.com/khovratovich/equihash).
- Aumasson, Neves, Wilcox-O'Hearn, Winnerlein, "BLAKE2: simpler, smaller,
  fast as MD5," 2013-01-29 — [paper](https://www.blake2.net/blake2.pdf),
  [site](https://blake2.net) — the hash primitive `hash=blake2b` (§3) is
  built on; this spec's own digest-length and personalization conventions
  are this project's choices layered on top, not asserted by the paper
  itself.
- Tang, Sun, Gong, "On the Regularity of the Generalized Birthday Problem,"
  eprint 2025/1351 — [paper](https://eprint.iacr.org/2025/1351) §5.2 (the
  regularity repair `keying=regular` implements), [artifact
  repo](https://github.com/tl2cents/Generalized-Birthday-Problem) (the
  paper's own name for this construction is "Sequihash"; see `SIZING.md`
  §0 and `UNIHASH.md` for the naming note and encoding comparison).
- `zcash/zcash`, `src/crypto/equihash.{h,cpp,tcc}` — the deployed
  Zcash-family convention this spec's `keying=single` and `expand_array`/
  `compress_array` byte layouts follow, referred to below as "zcashd."
  Local clone: `~/Work/ZK/ZKs/zcash`.
- `tromp/equihash`, `equi_miner.c`/`equi.h` — the optimized C solver whose
  bucket/index-pointer structure this project's own `Req/rust/src/solve/`
  backends re-derive; full commit history and provenance in
  `~/Work/ZK/Requihash/SOLVERS.md`. Local clone:
  `~/Work/ZK/ZKs/equihash-tromp`; vendored pinned copy at
  `~/.cargo/registry/.../equihash-0.3.0/tromp/`.
- `zebra-chain`, `src/work/equihash.rs` — the Rust verifier-convention
  reference this crate's `rust/` half follows. Local clone:
  `~/Work/ZK/ZKs/zebra`.

## 1. Status of configuration points

| Configuration | Status |
|---|---|
| `hash=blake2b, m=1, keying=regular` (Requihash) | **Implemented**, cross-validated C++/Rust, vectors in `vectors/` |
| `hash=blake2b, m=1, keying=single` (Equihash-style) | Specified (§4.3); not implemented in this tree |
| `hash=blake2b, m≥2` | Specified (§5); not implemented |
| `hash=blake3` (any) | Specified (§6); not implemented |
| Compact solution encoding | Sized (§8.2); codec not implemented |
| `encoding` axis (leaf-string byte serialization, separate from `keying`) | **Proposed, not adopted** — see `UNIHASH.md` (`binary_le32`, current/implicit, vs. `ascii_decimal`, matches the paper's own Python Sequihash reference) |

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

**Digest length.** `hash_output = ⌊512/n⌋ · n/8` bytes (following zcashd's
digest-length convention, one workable choice among others). Only the first `n/8` bytes are consensus-relevant (§4); the tail is
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
padding (`expand_array` with `bit_len = ℓ`, `byte_pad = 0`; written to be
interoperable, byte-accurate with zcashd's `ExpandArray` — not yet checked
against real zcashd output byte-for-byte, since the available KAT vectors
(`Req/PLAN.md` A14) are `keying=single` and this engine only implements
`keying=regular`; closing this gap is tracked as `Req/PLAN.md` A19). Byte comparison of a `cbyte`
segment is then exactly ℓ-bit comparison. The expanded row is representation, not consensus; the
consensus object is the n-bit leaf string.

### 4.3 `keying = single` (specified)

    D(i) = finalize( clone(S0).update(le32(i)) )

One `le32` word; no class word. (Note this is still one call *per leaf* —
a different choice than Zcash Equihash's `⌊512/n⌋`-leaves-per-call packing,
not a claim that this way is simpler or better. Per-call packing in single
mode is a permitted future optimization only if specified as a new
configuration point; it changes recomputation granularity and therefore
TMTO accounting.)

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

`(ℓ+1)` bits per index, big-endian bit-packed (`compress_array`, written to
be interoperable, byte-accurate with zcashd — the wire-size count matches
the paper's published Table 3 exactly, `1344 at (200,9)` per
`table3_wire_sizes`, but byte-for-byte content has not been checked against
real zcashd output; same verification gap as §4.2's `expand_array`,
tracked as `Req/PLAN.md` A19): wire size `2^k · (ℓ+1) / 8` bytes — 1344
at (200,9). Round-trips via `get_minimal_from_indices` /
`get_indices_from_minimal`.

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
