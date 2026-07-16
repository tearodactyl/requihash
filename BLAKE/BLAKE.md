# BLAKE.md — the BLAKE hash family: decision record, theory, provenance, APIs, and this repository's own BLAKE work

Single home for everything BLAKE-related in this program. Domain
boundaries: [../Dirs.md](../Dirs.md) owns the directory/repo map;
[../PAPERS.md](../PAPERS.md) owns research-paper citations (Equihash/GBP
theory); [../Equihash.md](../Equihash.md) owns the Equihash record and
points here for hash-primitive matters.

Subdirectories: [vendor/blake2/](vendor/blake2/) — the canonical
vendored implementation (see §0); [RFC/](RFC/) — RFC 7693 (txt + PDF)
and its extracted, self-test-validated reference code.

## 0. Decision record (2026-07-16)

**Canonical BLAKE2b for every C/C++ consumer in this repository: the
authors' portable reference (`ref/blake2b-ref.c`), vendored at
[vendor/blake2/](vendor/blake2/)** (three files, unmodified, commit and
license in `PROVENANCE.md` there), referenced **repo-relative only** —
no `$HOME`-absolute paths in any build file (an override variable
exists per consumer for exceptional cases: `RZ_BLAKE2_REF_DIR`,
`-DBLAKE2_REF_DIR`). **Canonical BLAKE2b for Rust consumers:
`blake2b_simd`** (§4.4).

Alternatives considered and rejected as the canonical C/C++ choice:

| Alternative | Why not |
|---|---|
| RFC 7693 appendix code | no parameter block → no personalization → cannot express Equihash (§3.4) |
| libb2 (system library) | autotools-only (poor Windows story), packaging overhead for what is one CC0 C file, dormant upstream |
| libsodium | correct and maintained, but a large dependency for one primitive; right where it's already present (Zero400), wrong as a new dependency for standalone tools |
| OpenSSL EVP | no personalization exposed (§3.4) — disqualifying |
| 2016 vendored snapshots (Khovratovich/tromp bundles) | don't compile on current toolchains (§6); superseded by this decision |
| BLAKE3 | a different hash — remains a Seam A *candidate*, not a BLAKE2b substitute |

**Interaction with the tromp codebase: none needed at the BLAKE2b level
anymore.** tromp's `equi_miner` builds **natively on arm64 as straight
portable C++** by pointing the include path at the modern reference
header (an include-directory shim mapping `blake/blake2.h` → the
vendored `blake2.h`) and linking `blake2b-ref.c` — verified 2026-07-16:
the native arm64 binary reproduces the true x86 SSE original's solve
trace exactly, thread-count-invariant (`Req/SOLVER_CORPUS.md` RT
section). This works because tromp's modified state layout is an
internal optimization, not a wire format: his variant produces standard
BLAKE2b digests (the cross-compiled SSE original matches the published
BLAKE2b-512("abc") vector). Consequences: **Rosetta/x86 emulation is
demoted** to an optional second oracle, not a build strategy; the
previous packed-struct header patch and the portable-glue patch set are
retired (superseded by, respectively, the include-shim and
`Req/SOLVER_CORPUS/rk/original/`).

## 1. The family

| Function | Words | Rounds | Block | Max digest | Salt/personal | Built for |
|---|---|---|---|---|---|---|
| BLAKE2**b** | 64-bit | 12 | 128 B | 64 B | 16 B each | 64-bit CPUs |
| BLAKE2**s** | 32-bit | 10 | 64 B | 32 B | 8 B each | 8–32-bit / embedded |
| BLAKE2**bp** / **sp** | as b/s | | | | | 4-lane / 8-lane parallel — *different functions with different outputs*, not implementations of 2b/2s |
| BLAKE2**x**b/s | as b/s | | | arbitrary (XOF) | | extendable output |
| BLAKE3 | 32-bit | 7 | 64 B | arbitrary (XOF) | derive-key mode | tree-parallel, SIMD-first |

Equihash uses BLAKE2**b** exclusively: it needs the 16-byte `personal`
field (§3.4) and ≤64-byte digests sized `HASHOUT = (512/n)·(n/8)`.
BLAKE3 appears only as `Req/`'s experimental `hash=blake3` backend
(`Req/SPEC.md` §6; ARM campaign answered, `Req/BENCHMARK.md` §9).

## 2. Getting BLAKE2: paper, RFC, and distribution channels

**Design paper**: Aumasson, Neves, Wilcox-O'Hearn, Winnerlein (2013),
"BLAKE2: simpler, smaller, fast as MD5"
([paper](https://www.blake2.net/blake2.pdf), [site](https://blake2.net))
— defines the full parameter block (digest/key length, fanout, depth,
tree fields, salt, **personal**).

**RFC 7693** (Saarinen & Aumasson, 2015): standards-track description
with its own, independent appendix implementation — now extracted and
validated locally at [RFC/code/](RFC/code/). Plain/keyed hashing only;
**no parameter block** (§3.4 for why and what that means).

**Channels** (how implementations reach programs), with toolchain
expectations and CPU-dispatch behavior:

| Channel | What it is | Toolchain / platform | SIMD selection |
|---|---|---|---|
| [BLAKE2/BLAKE2](https://github.com/BLAKE2/BLAKE2) package | authors' source drop, meant for vendoring: `ref/` (portable C99), `sse/` (SSE2→AVX `__m128i` C), `neon/` (ARM), `power8/` | any C compiler for `ref/`; x86 + `-msse2/-mssse3/-msse4.1/-mavx` for `sse/` (selected via `blake2-config.h` `HAVE_*` macros) | **compile-time only** — no runtime dispatch anywhere in the package |
| [libb2](https://github.com/BLAKE2/libb2) | the team's installable library (`libb2.so`/`.pc`; Debian `libb2-dev`) | autotools (`autogen.sh && configure && make`): fine on Linux and macOS (arm64 builds the portable path); Windows only via MSYS2/MinGW or vcpkg's port — no native MSVC build | configure-time by default; `--enable-fat` builds multi-arch x86 objects with **runtime** selection; `--enable-native` pins to build-machine CPU |
| libsodium | bundles its own package-derived BLAKE2b as `crypto_generichash` | everywhere (its whole point); Zero400 pins 1.0.21 via `depends/` | **runtime** (its `cpu_features` picker: ref/SSSE3/SSE4.1/AVX2 on x86) |
| OpenSSL | BLAKE2b-512/BLAKE2s-256 since 1.1.0, EVP interface | everywhere OpenSSL is | portable C implementation (BLAKE2 is not in OpenSSL's asm set) |
| Python `hashlib` | CPython bundles its own copy | in every Python ≥3.6 | portable |
| Rust crates | §4.4 | cargo | `blake2b_simd`/`blake3`: **runtime** (`is_x86_feature_detected!` / dispatch tables) |

The package's triple license (CC0 / OpenSSL / Apache-2.0) exists
precisely so OpenSSL could absorb the code. In 2016 none of the
linkable channels were ubiquitous, so cryptocurrency projects vendored
package variants wholesale — the origin of every §6 problem.

**Local clones** (all under `~/Work/ZK/ZKs/BLAKE/`): `blake2-reference`
(= BLAKE2/BLAKE2, shallow @ `ed1974e` 2023-02-12 = upstream tip),
`libb2`, `blake2_simd`, `BLAKE3`, `BLAKE3-specs`. Build-time source of
truth is **not** the clones but [vendor/blake2/](vendor/blake2/) (§0).

## 3. APIs

### 3.1 The flavors

1. **Simple one-shot** — `blake2b(out, outlen, in, inlen, key, keylen)`.
2. **Streaming** — `init*` → `update` → `final` over a copyable
   `blake2b_state` (§3.2).
3. **Parameter-block init** — `blake2b_init_param(S, P)`: the only
   flavor exposing `personal` (§3.4); the one Equihash requires.
4. **Keyed** (built-in MAC; Equihash uses `keylen=0`).
5. **Wrapped library APIs** — libsodium `crypto_generichash` (+
   `crypto_generichash_blake2b_salt_personal`), OpenSSL EVP, Python
   `hashlib.blake2b(digest_size=, key=, salt=, person=, …)` — the
   fullest high-level exposure of the parameter block anywhere, which is
   why the Sequihash Python reference "just works" —
   `blake2b_simd`'s builder `Params` (`.hash_length() .personal() …`) +
   `many::hash_many` batches.
6. **Miner-specialized** — tromp's `blake2bip` (§5.3).

### 3.2 Streaming, midstates, and who needs them

`blake2b_update()` exists for ordinary reasons — hashing data that
arrives in pieces (files, streams) without buffering it whole. Miners
need something stronger that falls out of the same design: the state
struct is **copyable by value**, so a prefix shared by millions of
messages is hashed once. Equihash hashes
`personal-initialized-state ‖ 140-byte header+nonce ‖ le32(leaf_index)`
for ~2^21 leaf indices per attempt: the 140-byte prefix costs 2 compress
calls, done **once** into a midstate; each leaf then clones the state
(a struct copy), appends 4 bytes, finalizes (1 compress). Without the
midstate every leaf would pay 3 compress calls instead of ~1 — a 3x
difference on the hashing phase. Ordinary applications hash complete,
distinct messages and never notice this property; that is why "miners
specifically want flavor 2/3 with a cheap-to-copy state and everyone
else is happy with one-shot."

**What RZ's glue maps to what** (`Req/SOLVER_CORPUS/rz/cross_check_c/blake2b_glue.{c,h}`):
the vendored `equihash` crate's `tromp/blake2b.h` deliberately exposes
an *opaque* miner-shaped interface — `BLAKE2bState*` behind
`blake2b_init(digest_len, personal)` / `blake2b_clone` / `blake2b_update`
/ `blake2b_finalize` / `blake2b_free` (designed so Rust's `blake2b_simd`
could sit behind it via FFI). The glue implements those five symbols on
top of the reference implementation: `init(len, personal)` → build a
`blake2b_param` with `digest_length=len`, `fanout=depth=1`,
`personal[16]`, call `blake2b_init_param`; `clone` → `malloc`+`memcpy`
of the state struct; `update`/`finalize` → `blake2b_update`/
`blake2b_final`. I.e., it maps the crate's opaque miner API onto flavor
3 + the copyable-state property.

### 3.3 A C-linkage warning (the argument-order trap)

The 2016-era package snapshots declare
`blake2b(out, in, key, outlen, inlen, keylen)`; the modern reference
declares `blake2b(out, outlen, in, inlen, key, keylen)`. Same name, six
arguments either way. **C symbols carry no parameter type or order
information** — there is no name mangling, so an object file compiled
against one declaration links silently against the other definition and
corrupts at runtime. (C++ mangles types into symbols and would fail the
link, but these are `extern "C"` functions, so that protection is
forfeited.) Header files do declare parameter types — the compiler
checks *call sites against the declaration it sees* — but nothing
checks the declaration against the *definition* in another translation
unit. The robust fix is coding directly to the modern order
(`rk/original` does); a shim only relocates the hazard.

### 3.4 Personalization — the deep dive

**What it is**: 16 bytes of the parameter block XOR-folded into the
initial state — a domain separator, cryptographically equivalent to a
different hash function per value. Equihash sets
`"ZcashPoW" ‖ le32(n) ‖ le32(k)` (Requihash: `"ReqhashPoW" ‖ le32(n) ‖
le16(k)`, `Req/SPEC.md` §3): solutions for one chain/parameter set are
meaningless for any other *by construction*, before any protocol rule.

**Connection to block information and the verification path**: the
personalization separates *domains*; the **block binds through the
update stream** — `state = init_param(personal); update(state,
header‖nonce)` where the 140-byte header includes the Merkle root (so
the transaction set) and the nonce. Verification recomputes exactly
this midstate from the received block header, then per solution index
recomputes the leaf hash and checks the Wagner-tree constraints
(ordering, collisions, zero XOR at root) — `Req/SPEC.md` §7,
`Req/SECURITY_ANALYSIS.md` "block binding". So: `personal` pins *which
puzzle family*, the updated header pins *which block instance*, and the
index set is the *witness* — three layers, all flowing through the one
midstate construction of §3.2.

**Why the RFC omits it**: deliberate scoping, not oversight — the RFC
standardizes the sequential core ("we have omitted description of tree
hashing modes... and also some optional features such as salting" —
RFC 7693 §1) to keep the Informational document minimal and
HMAC-replacement-shaped. Use-case dependent, exactly: general-purpose
hashing doesn't need `personal`; protocol designers (Zcash, Argon2's
internal use, this project) do. The consequence is real, though: any
stack built strictly on RFC-shaped interfaces — **OpenSSL EVP included:
fixed 512-bit `EVP_blake2b512`, no salt/personal params** — cannot
express Equihash. libsodium *does* support it, but through the
less-advertised `crypto_generichash_blake2b_salt_personal` init
variant rather than the headline API — "questionable" is fair in the
sense that it's easy to miss and easy for a wrapper library to not
re-export; it is however fully functional (it is what zcashd used,
2016–2020).

## 4. Implementations in the wild — provenance and versions

### 4.1 The package and its vendored descendants
See §2 (channels) and §6 (the 2016 snapshots this repo inherits).

### 4.2 libsodium
Its own maintained fork of package-lineage code behind
`crypto_generichash*`; runtime CPU dispatch. **Zero400 pins libsodium
1.0.21** (`depends/packages/libsodium.mk`) and uses, besides BLAKE2b
(77 call sites via `crypto_generichash*`): **Ed25519**
(`crypto_sign_*`, ~63 sites — JoinSplit signatures), **ChaCha20-Poly1305
IETF** (`crypto_aead_chacha20poly1305_ietf_*`, 17 — note encryption),
and **Curve25519** (`crypto_scalarmult*`, 10 — note-encryption key
agreement). So sodium is load-bearing in Zero400 well beyond hashing —
any "replace sodium" idea is a four-primitive migration, not one.

### 4.3 OpenSSL
BLAKE2b-512/BLAKE2s-256 since 1.1.0 (2016), contributed from the
triple-licensed package code; EVP-only, fixed digest length at the
legacy API (3.x providers allow size via params), no
salt/personal — not Equihash-capable (§3.4).

### 4.4 Rust crates
- **`blake2b_simd` / `blake2s_simd`** (Jack O'Connor,
  [repo](https://github.com/oconnor663/blake2_simd), MIT): independent
  pure-Rust implementation (not a C binding) with AVX2/SSE4.1 paths and
  **runtime detection**; `many::hash_many` batch API. Features:
  `std` (default), `uninline_portable` — i.e., no configuration needed
  for correctness or dispatch. Pinned here: 1.0.4 (`Req/rust`), 1.0.2
  (inside the pinned `equihash` crate). **How it earns its keep** (§0's
  Rust-side pick): (1) full parameter block including `personal` via
  builder `Params` — Equihash-capable where RustCrypto's `blake2`
  historically made personalization awkward; (2) `hash_many` is
  exactly the many-independent-short-messages shape of Equihash leaf
  generation, with runtime-dispatched SIMD — the maintained, standard
  equivalent of what tromp hand-built as `blake2bip` (§5.3); (3)
  already the ecosystem's converged choice: zcashd (2020 rewiring),
  librustzcash/`equihash` crate, zebra — so Zebro inherits it with
  zero decisions; (4) same author as official BLAKE3, actively
  maintained, no `unsafe` C FFI.
- **`blake2`** (RustCrypto): independent pure-Rust from spec,
  `Digest`-trait-shaped. Not used anywhere in this program (not even in
  the cargo cache) — listed for orientation only.
- **`blake3`** (official, BLAKE3 team): Rust + vendored C/asm kernels
  via `build.rs`, runtime dispatch; has a `neon` feature (plus
  `no_neon`/`no_avx2`/… opt-outs) — NEON is real and default-detected
  on aarch64 for BLAKE3, in contrast to BLAKE2b (§5.2). Pinned here:
  1.8.5 (`Req/rust`, feature-gated backend).

### 4.5 Python
`hashlib.blake2b` / `blake2s` in the standard library (CPython bundles
its own implementation): one-shot and streaming, and the **fullest
parameter-block exposure of any mainstream API** — `digest_size`,
`key`, `salt`, `person`, and the tree parameters, as keyword
arguments. `hashlib.blake2b(person=b"ZcashPoW"+..., digest_size=50)`
is a one-liner. BLAKE3 is third-party (`pip install blake3`,
Rust-backed).

## 5. Acceleration: x86 SIMD vs. GPU/CUDA vs. NEON vs. multicore

### 5.1 The four options, for BLAKE2b-in-Equihash specifically

| Approach | Mechanism | Gain on hashing | Caveats |
|---|---|---|---|
| x86 SIMD, one message (`sse/` lineage) | vectorize the G-function's 4×64-bit lanes within one hash | ~1.5–2x over scalar | diminishing: one message has limited internal parallelism |
| x86 SIMD, **interleaved batch** (AVX2: `blake2bip`, `blake2b_simd::many`) | run 4–8 *independent* hashes across SIMD lanes | ~4x+ over scalar per core | needs the caller to batch (Equihash leaf loop batches naturally) |
| NEON (aarch64) | same idea, 128-bit registers | marginal for BLAKE2**b**: 64-bit words leave only 2 lanes, and NEON lacks a 64-bit rotate (emulated in 2–3 ops) — the package's own `neon/` code has *reports of running slower than scalar* on some cores | this is a BLAKE2b-specific weakness; BLAKE3 (32-bit words, 4 lanes) vectorizes well on NEON |
| GPU / CUDA (SILENTARMY, nheqminer kernels, tromp's `eqcuda`) | thousands of leaf hashes + the whole Wagner sort/merge on-device | orders of magnitude on *throughput* | only pays at miner scale; PCIe latency and kernel-launch overhead swamp single-solve use; the real win historically was GPU-resident *sorting*, not just hashing |
| Multicore | parallelize across nonces (embarrassingly parallel) or across buckets within a solve (tromp's `pthread_barrier` rounds; Req Q1's rayon plan) | near-linear across nonces | orthogonal to and composable with all of the above |

### 5.2 What this means here (Amdahl, with our own numbers)

`Req/BENCHMARK.md`: hashing is ~17% of solve time at small parameters —
the merge dominates. So SIMD hashing is capped at ~1.2x whole-solve
until the merge is parallelized/optimized; batching+SIMD matters much
more at (200,9)-scale generation (2M leaves) and for miner throughput.
**Verdict on the NEON mention in BENCHMARK §9 / PLAN A13: yes,
premature.** BLAKE2b-on-NEON is intrinsically weak (2 lanes, emulated
rotates, slower-than-scalar reports), `blake2b_simd`'s portable path is
already fine on ARM, hashing is 17% of the pie, and no consumer demands
it. A13 is demoted to icebox accordingly (`Req/PLAN.md`); if ARM hash
throughput ever matters, the better lever is BLAKE3 (real NEON) or
batching, not a BLAKE2b NEON port.

### 5.3 Why tromp changed the struct and API, and the standard way now

Tromp's `blake/blake2.h` compacts `blake2b_state` from the standard
`{h[8], t[2], f[2], buf[128], buflen, outlen, last_node}` (~240 B) to
`{h[8], buf[128], u16 counter, u8 buflen, u8 lastblock}` (196 B):
Equihash inputs are ≤144 bytes, so a 128-bit block counter and two-word
finalization flags are dead weight — shrinking the state cuts the cost
of the **per-leaf state copy** (§3.2), the hot operation, and
simplifies round-4 setup. It stays digest-compatible with standard
BLAKE2b in that range (verified against the published "abc" vector).
`blake2bip` ("interleaved parallel") is the second step: finalize 4 or
8 *independent* leaves at once across AVX2 lanes from a shared midstate
(`blake2bx4_final(midstate, out, blockidx)`), i.e. hand-built batching.

**Standard alternatives for the same functions today**: the state-copy
midstate trick needs no custom struct — every implementation's state is
copyable (the compaction was a micro-optimization, worth it in 2016
assembly-race conditions, not structurally necessary — the native
portable RT build proves the algorithm runs fine on the standard
struct). The batching is `blake2b_simd::many::hash_many` (Rust,
runtime-dispatched, maintained) or Neves' own experimental
`blake2-avx2` repo (C); BLAKE3 gets batching + tree parallelism natively.

## 6. The 2016 vendored snapshots, and what breaks on current toolchains

| Copy | Lineage | Current status |
|---|---|---|
| `equihash-khovratovich/.../blake/` | package `sse/` verbatim (Neves, CC0) | ✗ unbuildable as-is; **superseded by the portable fork `Req/SOLVER_CORPUS/rk/original/`** (C++14, vendor BLAKE2b, modern arg order; regenerates all 8 vectors byte-identically) |
| `equihash-tromp/blake/` | package `sse/`, state compacted (§5.3) | ✗ unbuildable as-is; **superseded by the include-shim native build** (§0): point `blake/blake2.h` at the vendored modern header, link `blake2b-ref.c` — builds and matches on arm64 directly |
| `equihash-tromp/blake2-avx2/`, `blake2-asm/` | Neves AVX2 technique adapted; xenoncat's asm | x86-only by nature; not needed for any current use (batching story: §5.3) |
| `equihash-0.3.0/tromp/blake2b.h` (pinned crate) | opaque 5-function miner API | ✓ bridged by RZ's glue (§3.2) to the vendored reference |
| `Req/cpp/blake2b.h`, `Req/rust/src/blake2b.rs` | this project's own bundled scalar implementations | ✓ portable, KAT-tested |

Failure catalog on current Apple clang (all diagnosed 2026-07-15):
`-maes`/`-mavx` hard errors on arm64; unconditional `<emmintrin.h>`/
`__m128i` in `sse/` code; the 2016 header's `pack(1)`+`ALIGN(64)`
`blake2sp/bp` declarations rejected on *every* target (latent bug,
fixed upstream years ago — the modern header has no such structs
mis-declared, which is why the include-shim works); the §3.3
argument-order trap; `blake2b_state S[1]` local arrays under the same
alignment rule; and (adjacent, not BLAKE) `rdtsc()` x86 asm in
Khovratovich's `pow.cc`.

**With the §0 decision these are historical**: new work codes directly
against the modern vendored distribution — no shims, no adapters, no
per-consumer patches. The only surviving adapter is RZ's glue, which
exists because the *pinned crate's* API is opaque by design (its job is
matching that crate's C exactly, not working around header rot).

## 7. Consumers and the migration plan (proposed)

| Consumer | Today | Plan |
|---|---|---|
| `rk/original` (Khovratovich C++) | ✅ done — portable C++14 fork on vendor BLAKE2b, byte-identical vectors | none further |
| `rk` (Rust port) | own scalar via `blake2b_simd` | none — already on the Rust canonical |
| `rz` | vendored-crate C + glue → vendor BLAKE2b (repo-relative) | keep glue (see §6); done |
| `cs` | vendor BLAKE2b via CMake (repo-relative) | done |
| **RT** (unstarted port) | reference binary now buildable **natively** (§0) | build the native reference at each `(WN,RESTBITS)`, generate vectors incl. multi-thread invariance, then Rust port with `blake2b_simd` (`many` for leaf gen); Rosetta only as an optional second oracle |
| `Req/cpp` | bundles its own scalar `blake2b.h` | migrate to vendor BLAKE2b (delete the bundled copy) next time that file is touched — low priority, it's correct and KAT-gated today |
| `Req/rust` | bundled scalar + `blake2b_simd` feature + `blake3` feature | none — already the target shape (Seam A stays open per `ARCHITECTURE.md` §1a) |
| Zero400/ZeroPerf | libsodium 1.0.21 (`crypto_generichash` + Ed25519 + AEAD + scalarmult, §4.2) | **no change** — sodium is load-bearing across four primitives there; replacing it is out of scope and unjustified |
| Zebro | zebra → `equihash` crate → `blake2b_simd` | **no change needed** — already on the Rust canonical; a future Requihash solver in Zebro uses `blake2b_simd::Params::personal(...)` + `many`, i.e. the same stack, new personalization string |

Net: after this session only two open items remain — RT's port (now on
a clean native path) and an eventual `Req/cpp` unification onto the
vendored copy.

## 8. References

- Aumasson, Neves, Wilcox-O'Hearn, Winnerlein (2013), "BLAKE2: simpler,
  smaller, fast as MD5" — [paper](https://www.blake2.net/blake2.pdf),
  [site](https://blake2.net).
- RFC 7693 (Saarinen & Aumasson, 2015) — local copy + extracted,
  self-test-validated code: [RFC/](RFC/).
- [BLAKE2/BLAKE2](https://github.com/BLAKE2/BLAKE2),
  [BLAKE2/libb2](https://github.com/BLAKE2/libb2),
  [oconnor663/blake2_simd](https://github.com/oconnor663/blake2_simd),
  [BLAKE3-team/BLAKE3](https://github.com/BLAKE3-team/BLAKE3),
  [BLAKE3-team/BLAKE3-specs](https://github.com/BLAKE3-team/BLAKE3-specs)
  — local clones §2; vendored canonical copy [vendor/blake2/](vendor/blake2/).
- O'Connor, Aumasson, Neves, Wilcox-O'Hearn (2019), "BLAKE3: one
  function, fast everywhere" — behind `hash=blake3` (`Req/SPEC.md` §6).
