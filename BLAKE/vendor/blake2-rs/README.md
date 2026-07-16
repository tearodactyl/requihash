# blake2ref — Rust binding to the repository's vendored BLAKE2b

## Authorship

Written by this project, 2026-07-16 — both the Rust binding
(`src/lib.rs`) and the C accessor file (`csrc/blake2ref_glue.c`). No
upstream source; **zero lines of hash algorithm are authored here** —
the algorithm is entirely the vendored `../blake2/blake2b-ref.c`
(Neves), which this crate only binds. Test expectations come from
independent outside oracles (published "abc" vector, CPython `hashlib`,
`blake2b_simd`).

## Naming

Directory `blake2-rs`: the Rust-binding sibling of `../blake2` (the C it
binds). Crate name `blake2ref`: "BLAKE2, reference implementation" —
deliberately **not** `blake2`, which is RustCrypto's crates.io name;
shadowing it would invite dependency confusion even though this crate is
`publish = false`.

## Exactly which C

`../blake2/{blake2.h, blake2-impl.h, blake2b-ref.c}` — copied
**unmodified** from `github.com/BLAKE2/BLAKE2` `ref/` at commit
`ed1974e` (2023-02-12, upstream tip at vendoring; CC0 / OpenSSL /
Apache-2.0), per `../blake2/PROVENANCE.md`. The same three files
`Req/SOLVER_CORPUS/rk/original/`, `cs/`, and `rz/`'s cross-check
binaries compile. `csrc/blake2ref_glue.c` is this crate's own accessor
file *over* that API (parameter-block init from `(outlen, personal)`,
plus a `sizeof(blake2b_state)` reporter); the vendored files stay
untouched.

## Why an opaque state buffer

Rust never mirrors the C struct's fields. The state lives in an inline
`[u8; 256]` (no allocation; `Clone` = memcpy, so the per-leaf midstate
pattern is cheap) and the C side reports `sizeof(blake2b_state)` at
runtime, asserted against the capacity at init. A vendor update that
changes the struct can therefore never silently desynchronize a
duplicated Rust layout — it either fits (fine: the buffer is capacity,
not layout) or fails the assert loudly.

## Validation

`cargo test`: the published BLAKE2b-512("abc") vector; CPython
`hashlib.blake2b(person=…)` personalization vectors (independent
oracle); `blake2b_simd` agreement across digest lengths and
block-boundary-spanning input sizes; midstate-clone ≡ fresh-compute.

## Measured (this machine, aarch64, `cargo run --release --example bench`)

Leaf shape = Equihash miner pattern (140-byte midstate, clone + 4-byte
suffix + finalize(50) per leaf, 2^17 leaves); bulk = 1 MiB one-shot.

| | leaf ns (min/median) | bulk MB/s |
|---|---|---|
| blake2ref | 105.3 / 107.7 | 1634 |
| blake2b_simd (state clone) | 86.0 / 91.1 | 1703 |
| blake2b_simd (`many` batch) | 260.2 / 271.8 | — |
| blake3 (no personalization; XOF-truncated — different hash) | 78.3 / 79.1 | 2489 |

Readings: bulk parity says the two compress cores are equivalent — the
~20% leaf-shape gap is binding mechanics (three FFI calls per leaf and a
256-byte state copy vs. `blake2b_simd`'s inlined Rust and tighter
state). `many::hash_many` on aarch64 is **counterproductive** (its SIMD
batching is x86-only; on ARM it degrades to portable one-at-a-time under
batch-API overhead) — the batch API earns its keep only on AVX2
hardware, unmeasured here. `blake2ref`'s claim is uniform provenance
(same object code as every C/C++ consumer), not peak speed.

## Status

Built and validated; **not wired into `Req/rust`** — Seam A remains as
is (`PLAN.md` A22) pending the accelerated-implementation exercise on
x86 and the explicit go-ahead.
