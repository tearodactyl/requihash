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
`-DBLAKE2_REF_DIR`). **Uniform implementation across use cases (requirements-first,
2026-07-16)**: the requirements are BLAKE2b with the full parameter
block (`personal`), streaming with a cheaply copyable state (the
midstate pattern), portability across Linux/macOS/Windows on
arm64/x86_64, single provenance and auditability, and acceleration
added *within the implementation* as dispatch-selected kernel variants
(§5.4) — a rule scoped to the primitive's internals, not to consumer
seams, where independent implementations remain swappable measured
candidates behind equivalence gates (origin, justification, and exact
scope: `UniBlake.md` §1b). One implementation satisfies all of it:
the vendored reference — for C/C++, which consume it directly. For
Rust, applying the project's own change criterion (an incumbent stays
unless a challenger is clearly superior in tight code, performance, or
compatibility) resolves it the other way: **`Req/rust`'s bundled
single-file scalar (`src/blake2b.rs`, 153 lines) stays as the Rust
reference hasher.** It is a complete, clean parameter-block
implementation — zero dependencies, zero `unsafe`, no build script,
correct 128-bit length counter, plain-copy `Clone` for the midstate
pattern — auditable in one sitting and portable everywhere cargo goes,
including targets with no C toolchain. No challenger beats it on the
criterion: `blake2ref` adds FFI + a `cc` build dependency for equal
bulk throughput, and `blake2b_simd` adds ~4,200 third-party lines whose
payoff (runtime AVX2/SSE4.1, `many` batching) exists only on x86.
So uniformity here means *one specification, minimal cross-validated
implementations*: the vendored C for C/C++, the single audited Rust
file for Rust — with **[vendor/blake2-rs/](vendor/blake2-rs/)
(`blake2ref`)** kept as the by-construction cross-language consistency
artifact (bound to the exact vendored object code; validated against
the published "abc" vector, CPython `hashlib` personalization vectors,
and `blake2b_simd`; available to any future consumer that genuinely
needs C-object parity — a production-validated construction: CKB ships
consensus hashing through exactly this wrapper genre, see
[vendor/blake2-rs/README.md](vendor/blake2-rs/README.md) "Comparable
products"). `blake2b_simd` is a *measured x86-acceleration
candidate at Seam A* — nothing more. No Seam A rewiring is planned.
(Forward note: under the UniBlake proposal, vendoring-vs-revamp-vs-
originate becomes a per-component decision governed by conformance
gates and documented provenance, not doctrine — `UniBlake.md` §1a.
The vendored-unmodified stance above describes today's consumers, not
a permanent constraint.)

Alternatives considered and rejected as the canonical C/C++ choice:

| Alternative | Why not |
|---|---|
| RFC 7693 appendix code | no parameter block → no personalization → cannot express Equihash (§3.4) |
| libb2 (system library) | official and same-vintage as the reference (2023 sync, key-length fix present) — "dead" is not the issue; the issues are concrete: x86-only dispatch (no NEON; AVX2 commented out in `blake2-dispatch.c`), an open data race in its fat-mode function-pointer install (its #39, 2022), broken detection on arm64 macOS (its #36 — our machine class), aging autotools (its #40; no CMake/MSVC), no release since 0.98.1 so distros ship older code, and it retains the legacy 2016 argument order (`blake2b(out, in, key, …)` — its #47), i.e. §3.3's trap institutionalized |
| libsodium | correct and maintained, but a large dependency for one primitive; right where it's already present (Zero400), wrong as a new dependency for standalone tools |
| OpenSSL EVP | no personalization exposed (§3.4) — disqualifying |
| 2016 vendored snapshots (Khovratovich/tromp bundles) | don't compile on current toolchains (§6); superseded by this decision |
| BLAKE3 | a different hash — remains a Seam A *candidate*, not a BLAKE2b substitute |

**Interaction with the tromp codebase: none needed at the BLAKE2b
level.** tromp's `equi_miner` builds **natively as straight portable
C++** against the vendored modern header and `blake2b-ref.c` — verified:
solve trace identical to the upstream x86 build, thread-count-invariant
(`Req/SOLVER_CORPUS.md` RT section). His modified state layout is an
internal optimization, not a wire format: it produces standard BLAKE2b
digests, so the standard layout drops in via the header alone.

**Shim/adapter policy**: committed work codes directly to the modern
distribution — no shims. An include-directory shim (mapping
`blake/blake2.h` → the vendored header) is permitted only as an
*evaluation* tool for running pristine upstream sources unmodified;
any port forks and codes to the vendored header directly, as
`rk/original` does. The one standing adapter is RZ's glue, which is not
a workaround: it *implements a required opaque interface* that the
pinned `equihash` crate's vendored C defines (§3.2), and RZ's whole
purpose is byte-fidelity to that exact crate.

**Scope of the pinned `equihash` crate (provenance and allowed roles)**:
published by Jack Grigg (Electric Coin Company) from
`zcash/librustzcash` (`components/equihash`, MIT OR Apache-2.0);
version history 0.1.0 (2020) → 0.2.x (2022–2025) → **0.3.0
(2026-04-24, the newest published release)**. It reaches this machine
through Zebro's `Cargo.lock` (zebrad → zebra-chain → `equihash`), and
zebra HEAD still depends on it. So the *crate* is current; what is
genuinely old is by upstream design: the C solver vendored **inside**
it is zcashd's copy of tromp's code frozen at `690fc5eff` (2016),
deliberately never resynced (consensus artifact), single-core-stripped
in 2024. Allowed roles here — all *oracle*, never design input: RZ's
fidelity target (matching that exact C is RZ's purpose), A19's
bit-packing test oracle, A14's KAT source (already extracted to static
vectors). It imposes no constraint on this program's own
implementations.

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
| [libb2](https://github.com/BLAKE2/libb2) | the team's installable library (`libb2.so`/`.pc`; Debian `libb2-dev`) | autotools (`autogen.sh && configure && make`): fine on Linux and macOS (arm64 builds the portable path); Windows only via MSYS2/MinGW or vcpkg's port — no native MSVC build | configure-time by default; `--enable-fat` = runtime **x86-only** selection via `blake2-dispatch.c` (SSE2→AVX; AVX2 commented out; no NEON; open data-race issue #39; arm64-macOS detection issue #36); `--enable-native` pins to build CPU. Simple API keeps the legacy 2016 argument order (#47) |
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
`libb2`, `blake2_simd`, `RustCrypto-hashes` (shallow @ `f6c786d`
2026-07-16; RustCrypto's `blake2` is a subdirectory of that monorepo,
0.11.0-rc.6 at this commit), `BLAKE3`, `BLAKE3-specs`. Build-time source of
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
(`rk/original` does); a shim only relocates the hazard. Headers *do*
carry full parameter types, and within one lineage that protects you:
with the modern header included, a 2016-order call such as
`blake2b(buf, &input, NULL, …)` fails to compile (`&input` lands on the
`size_t outlen` parameter) — which is exactly how `rk/original`'s two
call sites were caught by the compiler rather than at runtime. The
silent-corruption case is specifically *mixed lineages*: code compiled
against one lineage's header, linked against the other lineage's object
file.

### 3.4 Personalization — the deep dive

**What it is**: 16 bytes of the parameter block XOR-folded into the
initial state — a domain separator, cryptographically equivalent to a
different hash function per value. Equihash sets
`"ZcashPoW" ‖ le32(n) ‖ le32(k)` (Requihash: `"ReqPoW" ‖ reserved[4] ‖
le32(n) ‖ le16(k)`, `Req/SPEC.md` §3): solutions for one chain/parameter set are
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
  (inside the pinned `equihash` crate). **Why it is the accelerated
  tier here** — technical merits only: (1) full parameter block
  including `personal` via builder `Params`; (2) `hash_many` is exactly
  the many-independent-short-messages shape of Equihash leaf
  generation, with runtime-dispatched SIMD — the maintained equivalent
  of what tromp hand-built as `blake2bip` (§5.3); (3) pure Rust, no FFI
  or build coupling. That zcashd, librustzcash, and zebra also wrap it
  (zebra via the pinned `equihash` crate, at 1.0.2) is **inheritance,
  not adoption evidence**, and carries no weight in this decision —
  this project treats every backend as a measured candidate
  (`Req/ARCHITECTURE.md` §1a), and Zebro inheriting zebra's choice is
  precisely the kind of default this program exists to re-examine.
  **Why not wrap the vendored `blake2b-ref.c` for Rust instead?** A
  real alternative with one strong property — byte-provenance-by-
  construction, the same C object code across every language — at the
  cost of `unsafe` FFI, a `cc` build dependency, and no batching. This
  repository currently gets equivalent assurance differently:
  `Req/rust`'s own scalar is KAT-gated and cross-validated
  byte-for-byte against the C++ side, and `blake2b_simd` must pass a
  runtime self-test against that scalar before autodetection adopts it.
  The FFI wrap stays on the table as the documented fallback if
  construction-level identity is ever demanded (e.g., a consensus audit
  requirement).
- **`blake2`** (RustCrypto): independent pure-Rust from spec,
  `Digest`/`Mac`-trait-shaped; the ecosystem's most-used BLAKE2 crate
  (136M downloads vs. `blake2b_simd`'s 51M as of 2026-07), actively
  maintained (0.10.x stable; 0.11.0-rc series through 2026).
  **Personalization is supported** (verified in 0.10.6 source:
  `new_with_salt_and_personal(key, salt, persona)` on the `*Var` types)
  — so it is Equihash-capable. Its `simd`/`simd_opt`/`simd_asm`
  features are the legacy nightly-era lineage, off by default and not
  runtime-dispatched; effectively portable in practice, no batch API.
  Evaluated and passed over for the seam on the change criterion: a
  dependency with trait machinery and some `unsafe` (byte-cast
  helpers), delivering nothing beyond the zero-dependency 153-line
  bundled scalar. The right default for a general Rust application; not
  needed here.
- **`blake2-rfc`**: the pre-2018 de-facto standard (13.7M downloads),
  unmaintained since 2017-11 — historical only, do not adopt.
- **C-wrapper crates** (`blake2b-rs` by Nervos/CKB — 1.05M downloads,
  consensus-proven, dormant since 2020, vendors its own unversioned
  package copies behind bindgen struct mirrors; `libsodium-sys-stable`;
  dead `libb2-sys`): the genre this repository's own `blake2ref`
  belongs to — full comparison and the first-party justification in
  [vendor/blake2-rs/README.md](vendor/blake2-rs/README.md).
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
| NEON (aarch64) | same idea, 128-bit registers | marginal-to-negative for BLAKE2b (measured; details in `Platforms.md` §5) | a BLAKE2b-specific weakness; BLAKE3 vectorizes well on NEON |
| GPU / CUDA (SILENTARMY, nheqminer kernels, tromp's `eqcuda`) | thousands of leaf hashes + the whole Wagner sort/merge on-device | orders of magnitude on *throughput* | only pays at miner scale; PCIe latency and kernel-launch overhead swamp single-solve use; the real win historically was GPU-resident *sorting*, not just hashing |
| Multicore | parallelize across nonces (embarrassingly parallel) or across buckets within a solve (tromp's `pthread_barrier` rounds; Req Q1's rayon plan) | near-linear across nonces | orthogonal to and composable with all of the above |

### 5.2 What this means here, and the NEON picture

`Req/BENCHMARK.md`: hashing is ~17% of solve time at small parameters —
the merge dominates — so any hash-side SIMD win is capped near 1.2x
whole-solve until the merge itself is addressed. Batching+SIMD matters
at (200,9)-scale generation and miner throughput.

**A13 (BLAKE2b NEON backend) is icebox for one primary reason: this
program is not at the optimization stage.** The current stage is
correctness, reference implementations, and measurement infrastructure
(A5/A6, the RT port); SIMD backend work — NEON included — is sequenced
after the measurements that would justify it, not before.

Technical notes for when that stage arrives. **Known BLAKE2 NEON
implementations**: the official package's `neon/` directory
(`blake2b-neon.c` + load headers, Neves lineage — ARMv7 and AArch64)
and Crypto++'s BLAKE2 NEON path (Walton/Neves); BLAKE3's NEON kernel
ships in its official implementations (a different hash, listed for
contrast); libsodium and OpenSSL carry none.

The NEON-vs-scalar performance picture (why AArch64 headroom is modest,
and the measured result) is out of scope for this doc — it lives in
[`Platforms.md`](Platforms.md) §5.

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

*uniblake's milder version of the same move*: its `ub_state` keeps the
full `{h,t,f,buf,buflen,outlen}` but drops only the trailing `last_node`
(sequential-only, no tree mode) — a small, safe scoping trim, not
tromp's aggressive compaction, and safe for the same structural reason
the RT build is: nothing outside the primitive depends on the layout
(`uniblake/STATUS.md` finding 5, `uniblake/src/ub_internal.h`).

**"If miners like it, why not us?" — we will, at the right stage.**
The compact state and midstate batching are real wins, and miners keep
them for good reason: the per-leaf state copy is the hot operation at
miner scale, and a smaller state plus interleaved finalization is worth
real percentage points. "Behaviorally equivalent" (§0) means *safe to
defer*, not unimportant — these techniques change performance, never
digests. This program's sequence: correctness and measurement first
(A5 counting harness, A6 index-pointer backend, the RT port), then
adopt the miner techniques deliberately where measurements justify
them — copy-cheap midstate state, `hash_many`-style batching, and
runtime dispatch (§5.4) are the shortlist.

### 5.4 Adding runtime detection/dispatch to the vendored distribution

The package's own selection is compile-time only (§2). When the
optimization stage wants one binary using SSE/AVX2 or NEON where
present: compile each compression variant as its own translation unit
with distinct symbols (`blake2b_compress_ref/_sse41/_avx2/_neon`), plus
a once-run initializer that probes the CPU — `cpuid` on x86,
`getauxval(AT_HWCAP)` on Linux/ARM32, `sysctlbyname` on macOS (AArch64
NEON is architecturally guaranteed, no probe needed) — and installs a
function pointer that the streaming API calls through. Three copyable
precedents: libb2's `--enable-fat`, libsodium's `cpu_features` picker,
BLAKE3's `blake3_dispatch.c`. **BLAKE3 needs nothing added**: its
official C and Rust already ship runtime x86 dispatch, and NEON is
compiled in on AArch64 (universal there; 32-bit ARM would need the
`getauxval` probe). Cost: one indirect call per compress or batch —
amortized invisible. Not scheduled — optimization-stage work, behind
the same self-test-gate discipline as `Req/rust`'s `simd` seam.


## 6. The 2016 vendored snapshots, and what breaks on current toolchains

| Copy | Lineage | Current status |
|---|---|---|
| `equihash-khovratovich/.../blake/` | package `sse/` verbatim (Neves, CC0) | ✗ unbuildable as-is; **superseded by the portable fork `Req/SOLVER_CORPUS/rk/original/`** (C++14, vendor BLAKE2b, modern arg order; regenerates all 8 vectors byte-identically) |
| `equihash-tromp/blake/` | package `sse/`, state compacted (§5.3) | ✗ unbuildable as-is; builds natively against the vendored modern header (§0) — pristine-source evaluation via include-shim only; RT's port forks to the vendored header directly |
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
| `rk` (Rust port) | hashes via `blake2b_simd` | none — appropriate for a port harness (not consensus code) |
| `rz` | vendored-crate C + glue → vendor BLAKE2b (repo-relative) | keep glue (see §6); done |
| `cs` | vendor BLAKE2b via CMake (repo-relative) | done |
| **RT** (unstarted port) | reference binary now buildable **natively** (§0) | build the native reference at each `(WN,RESTBITS)`, generate vectors incl. multi-thread invariance, then the Rust port (backend per §0's Rust tiers, batching per §5.3) |
| `Req/cpp` | bundles its own scalar `blake2b.h` | migrate to vendor BLAKE2b (delete the bundled copy) next time that file is touched — low priority, it's correct and KAT-gated today |
| `Req/rust` | bundled scalar + `blake2b_simd` feature + `blake3` feature | wire `blake2ref` (§0) in at Seam A as the reference hasher and retire the bundled scalar duplicate — **pending approval** (consensus-path change); `blake2b_simd`/`blake3` stay as measured candidates |
| Zero400/ZeroPerf | libsodium 1.0.21 (`crypto_generichash` + Ed25519 + AEAD + scalarmult, §4.2) | **no change** — sodium is load-bearing across four primitives there; replacing it is out of scope and unjustified |
| Zebro | zebra → `equihash` crate → `blake2b_simd` | **no change now** — but the inheritance is not treated as endorsement; a future Requihash solver in Zebro picks its backend on Req's measurements (Seam A posture), with `Params::personal(...)` + `many` the likely shape |

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
