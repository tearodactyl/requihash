# cs-rs — Rust port of the "Sequihash" k-list Wagner solver

Native Rust re-port of the 2025/1351 paper's own Python k-list solver,
faithful to the C++ port in [`../cs`](../cs) (both derive from
`k_list_algorithm.py`; the C++ port is this one's differential oracle).

## The two conventions (read `../cs/README.md` first)

Reproduced here **exactly**, not translated:

1. **`k` is a list count** (a power of 2 = the paper's `K`), not `Req`'s
   tree-depth exponent. In this API `k` **is** the solution size.
2. **Leaf encoding is ASCII `"i-j"`** appended to the raw 16-byte nonce
   (`nonce ++ format!("{i}-{j}")`), not `Req`'s binary
   `le32(i mod k) || le32(i div k)`.

## What it ports

`KListWagnerAlgorithm` in `src/lib.rs` — `new` (≈ `__init__`),
`solve`, `verify`, `compute_item`, and the internal `compute_hash_list`
/ `hash_merge` / `solve_internal`, method names tracking the Python
original. The big-endian arbitrary-precision helpers (`big_xor`,
`big_shr`, `low_bits_key`) mirror the C++ `klist.cpp` — including the
`big_shr` direction that was the one real bug found+fixed during the
original C++ port (a wrong-direction shift causes a collision explosion;
the smallest-point solve test and the differential vectors both catch
it). Only the no-trade-off solve path is ported (as in the C++ port).

**BLAKE2b**: plain unkeyed, no personalization, digest length `n/8` —
via `blake2b_simd` (pure-Rust, portable path), matching the C++ port's
`blake2b(digest, n/8, msg, len, NULL, 0)`.

## Layout (mirrors `../rz`)

- `src/lib.rs` — the algorithm.
- `src/bin/cs_gen.rs` — driver; prints the same JSON schema as C++
  `cs_gen` (`{"n","k","nonce_hex","solutions","verified"}`).
- `tests/differential.rs` — re-solves every vector in `../cs/vectors/`
  (one shared source of truth, from the Python reference) and asserts
  the solution SET matches (order-independent; the reference and this
  port both emit in hash-table order).

## Validation

`cargo test --release` — matches the Python reference on all 4 committed
vectors: `(24,8)`, `(40,16)`, `(64,128)`, and the 2-solution
`(160,512)` point (~16 s here). `cargo run --bin cs_gen 24 8
00112233445566778899aabbccddeeff` reproduces the committed
`[52,38,50,40,46,4,39,60]` exactly.
