# UniBlake — unified C/C++ BLAKE2 (and BLAKE3) setup

**Status: PROPOSAL, decision pending.** Design for discussion; nothing
below is built. On approval this becomes `BLAKE/unitblake/`, a PLAN
item, and the supersession target for §5.4's dispatch sketch and
icebox A13.

## 1. What it is

One C-core library presenting one API across every target platform
(Linux/macOS/Windows; arm64/x86_64; C99 core with a C++14-friendly
header), containing:

- the **single-CPU reference path** as the always-present baseline
  (today: the vendored `blake2b-ref.c` lineage; §1a governs whether it
  stays vendored, gets a conforming revamp, or both — with the
  untouched reference retained as oracle regardless);
- **runtime auto-detection and dispatch** (function-pointer table
  installed by a one-time CPU probe: `cpuid` / `getauxval(AT_HWCAP)` /
  `sysctlbyname`; AArch64 NEON needs no probe);
- **x86 (SSE4.1/AVX2) and NEON specializations** as separate
  translation units with distinct symbols — candidate donors are the
  package's `sse/`/`neon/` variants, libsodium's compress TUs, and
  Neves' AVX2 batch work, but each TU may equally be revamped or
  original where features/conformance/performance justify it (§1a);
- **structure for parallel/concurrent use**: a batch ("many") API for
  interleaved multi-message hashing, reentrant copyable states for the
  midstate pattern, and a clean statement of the threading boundary —
  hashing is embarrassingly parallel *across messages*, so UniBlake
  provides batch interfaces and thread-safe-by-value states while
  scheduling (thread pools, rayon, pthreads) stays in consumers;
- **pluggable variants including for the scalar reference itself**:
  registration + a forced-implementation override (`UB_FORCE_IMPL`,
  "force implementation": bypass CPU auto-detection and pin one named
  kernel — `ref`, `sse41`, `avx2`, `neon`, or an experimental variant —
  for the process; the benchmarking/reproduction tool, precedented by
  capability-mask overrides like `OPENSSL_ia32cap` and `GODEBUG=cpu.*`.
  A forced variant still passes the oracle self-test or hard-errors —
  forcing selects, it never bypasses the gate). Name negotiable.

## 1a. Code sourcing policy — vendoring is TBD, per component

Whether C/C++ code is vendored, and to what extent, is **decided per
component, not by doctrine**. Reference / widely-distributed code is
the preferred starting point *when it fits*; any component may be
revamped or originated outright for **feature** reasons (batch API,
experiment hooks — which no existing code has, §2b), **conformance**
reasons (modern argument order, C99/C++14 cleanliness, warning-free on
current toolchains), or **performance** reasons (measured, per the
bench gates). Two invariants replace "unmodified third-party bytes" as
the discipline:

1. **Conformance anchor**: at least one untouched reference
   implementation always remains linked as the oracle backend, and
   every variant — vendored, revamped, or original — passes the
   KAT/self-test gate against it. Correctness never rests on code
   origin.
2. **Documented provenance, pinned per release**: every UniBlake
   release carries a provenance manifest naming the exact upstream
   version/commit of every outside input in whichever mode it was
   used — **plugged in** (vendored-unmodified, commit pinned),
   **rewritten from** (derived; source commit + complete deviation
   list, `rk/original`-style), or **used as a direct example**
   (original code authored here, informed-by citation recorded,
   oracle-tested). Pinning is not a property of the vendored mode
   only — all three modes pin what they took and from where, so any
   release's audit path is reconstructible without this repository's
   history.

## 1b. The "no acceleration by library swapping" rule — origin, justification, scope

**Origin**: formulated 2026-07-16 in `BLAKE.md` §0's requirements list
(assistant's derivation from the uniformity directive, not a verbatim
owner requirement); scrutinized and scoped here on request.

**As an absolute it was improper**: `Req/rust`'s Seam A is deliberately
acceleration-by-swappable-implementation (`blake2b_simd` as a gated
candidate), and the program's standing posture is that backends stay
measured candidates. A program-wide "never" contradicted both.

**Properly scoped, it is sound — and nearly forced.** The rule binds
**UniBlake's internals**: within the primitive, acceleration is added
as compress-kernel variants behind one streaming core's dispatch; the
library never becomes a facade that selects among whole third-party
hashing libraries. Justification:

1. **Audit increment**: a kernel variant changes only the compress
   function; parameter-block handling, state layout, and finalization —
   the byte-shaping, consensus-relevant code — stay fixed. A
   whole-library swap changes all of it at once (zcashd swapped
   BLAKE2b backends four times, 2016–2022 — `../SOLVERS.md` §3 — each
   a full provenance discontinuity).
2. **Gate cheapness**: same-core kernel variants self-test against the
   in-tree oracle at startup for free; a library swap needs a
   full-surface equivalence campaign (appropriate at seams, where Req
   already does it — too heavy to repeat inside the primitive per
   CPU tier).
3. **Bisection**: a wrong digest or a regression isolates to one
   kernel translation unit.
4. **The anti-pattern is on record**: `blake2b-rs`'s `build.rs` swaps
   whole C files at compile time behind frozen bindgen mirrors; libb2
   stalled at AVX1 partly because variants weren't independently
   pluggable units (§2a/§2b).
5. **Runtime dispatch forces it anyway** (R3): per-CPU selection
   requires all variants to share one state layout and API — which is
   precisely "same implementation, variable kernel." Whole libraries
   with differing state layouts cannot be runtime-swapped per CPU.

**Two-layer statement, final**: *inside* UniBlake — one core, kernel
variants, oracle-gated, no library facades; *at consumer seams* (Req
Seam A and equivalents) — independent implementations, UniBlake itself
included, remain swappable measured candidates behind equivalence
gates. Consistent with §1a: kernels may be vendored, revamped, or
original; "same implementation" means the shared core/state/param
path plus the oracle gate, not any particular code origin.

## 2. The two design decisions

**Pluggability granularity: the compress function, not the whole
hash.** One streaming core (`init_param`/`update`/`final`/state-copy,
~100 lines, derived from the reference with attribution) calls
`compress(state, blocks, n)` through the dispatch table; variants are
compress implementations. This is exactly how libsodium's picker,
libb2's `--enable-fat`, and BLAKE3's `blake3_dispatch.c` work — the
proven shape. The pristine vendored `blake2b-ref.c` additionally stays
linked as an *oracle backend* (whole-implementation, no dispatch) so
every variant is self-tested against untouched reference bytes at
startup, extending the self-test-gate discipline Req's Seam A already
uses.

**BLAKE3 is adoption, not duplication.** BLAKE3's official C already
*is* UniBlake-shaped — `blake3_dispatch.c`, portable + SSE2/SSE4.1/
AVX2/AVX-512/NEON translation units, runtime detection. Duplicating
the structure means: vendor the official C the same way `blake2` is
vendored, and present it through the same `ub_` API family so both
hashes expose one interface. New construction ≈ zero; the work is API
unification and provenance pinning.

## 2a. Prior art: libb2, examined (2026-07-16)

libb2 is the official org's own attempt at exactly this library — and
it validates the design (compress-level runtime dispatch over the
reference lineage, CC0, same code vintage as the reference repo: its
2023 sync carries the key-length fix). It is *not* the base to build
on, for verified reasons from its own source and tracker: dispatch and
acceleration are x86-only (no NEON files exist; `AVX2` is literally
commented out in `blake2-dispatch.c`'s variant enum, so its best path
is AVX1-era); the fat-mode function-pointer install has an open data
race (#39, 2022); configure-time detection breaks on arm64 macOS (#36
— this machine's class); autotools bit-rot breaks modern installs
(#40) with CMake/Meson requests unanswered (#37/#38); no release since
0.98.1, so distributions package behind HEAD; and its simple API keeps
the legacy 2016 argument order (#47) — the §3.3 trap, institutionalized.
UniBlake is, in one sentence, *libb2 done for 2026*: ARM included,
AVX2 included, race-free one-time dispatch install, CMake, batch API,
and experiment hooks. Its CC0 `cpuid` and dispatch skeleton are
directly minable.

## 2b. The field vs. UniBlake's requirements (surveyed/verified 2026-07-16)

Requirements shorthand: **R1** unified build on Linux/macOS/Windows ×
arm64/x86_64 · **R2** always-present scalar reference path · **R3**
runtime detection+dispatch · **R4** modern x86 SIMD · **R5** NEON ·
**R6** batch/"many" API + copyable states · **R7** pluggable variants /
forced-impl experiment hooks · **R8** pinnable single provenance for
this tree · **R9** personalization.

| Implementation | R1 | R2 | R3 | R4 | R5 | R6 | R7 | R8 | R9 |
|---|---|---|---|---|---|---|---|---|---|
| BLAKE2/BLAKE2 package | ✗ source drop, no build system | ✓ | ✗ compile-time only | SSE2–AVX/XOP; **no AVX2** | ✓ `neon/` (single-msg) | ✗ (states copyable; no batch) | ✗ | **✓ (our vendor)** | ✓ |
| libb2 | ✗ autotools rot (#40), no MSVC | ✓ | x86-only, **race #39**, arm64-macOS detect broken #36 | SSE2–AVX/XOP; AVX2 commented out | ✗ | ✗ | ✗ | adds a 2nd copy | ✓ |
| libsodium | ✓ | ✓ | **✓ sound runtime picker** | SSSE3/SSE4.1/**AVX2** compress TUs (verified file listing) | ✗ | ✗ batch; state copyable | ✗ | whole crypto lib for one primitive | ✓ (`_salt_personal`) |
| blake2b-rs (Nervos) | cc-based; plausible MSVC | ✓ | ✗ compile-time (target features) | package `sse/` lineage (no AVX2) | ✗ | ✗ | ✗ | ✗ own unversioned 2020 copies | ✓ full builder |
| blake2 (RustCrypto) | ✓ (pure Rust) | ✓ | n/a | legacy off-default features only | ✗ | ✗ batch; states clonable | ✗ | crates.io pin (Rust-only provenance) | ✓ (verified) |
| blake2b_simd | ✓ (pure Rust) | ✓ `portable.rs` | ✓ (x86 only) | SSE4.1 + **AVX2 incl. 4-way `many`** | ✗ | ✓ `many::hash_many` (pays only on x86) | ✗ | crates.io pin (Rust-only provenance) | ✓ |
| OpenSSL EVP | ✓ | ✓ | n/a (portable only) | ✗ | ✗ | ✗ | ✗ | heavy | **✗ — disqualified** |
| *BLAKE3 official C (structural benchmark; different hash)* | ✓ | ✓ | ✓ `blake3_dispatch.c` | SSE2→AVX-512 | ✓ | tree-parallel inherent | ✗ | vendorable | n/a (derive-key model) |

**The finding that justifies UniBlake**: for BLAKE2b, the intersection
R3∧R4∧R5 (runtime dispatch + modern x86 + NEON) is **empty** across the
entire field — libsodium lacks NEON, the package's NEON lacks dispatch
and AVX2, `blake2b_simd` lacks NEON, libb2 lacks all three in practice.
R7 (experiment hooks) exists nowhere. UniBlake is not reinventing an
existing product; it is filling a verified hole, with per-axis donors:
libsodium's picker + AVX2 compress TUs (design to mine), the package's
`neon/` TU, `blake2b_simd`'s `many` shape and Neves' `blake2-avx2` for
batching, libb2 as the cautionary tale plus CC0 `cpuid` skeleton.

### x86 / NEON optimization breadth and quality, per implementation

Assessment method, stated: (1) ISA inventory from source; (2) technique
inspection — the quality tell for BLAKE2b is how rot24/rot16 are done
(byte-permute `pshufb`/`tbl` = good; shift-OR emulation = slow) and
whether 4×64-bit lanes are used (AVX2/128-bit = 2 lanes); (3) selection
mechanism soundness; (4) our own measurements where hardware exists
(aarch64 done — `vendor/blake2-rs/README.md`; x86 gated on A7); (5)
upstream fix history.

| Implementation | x86 breadth/quality | NEON breadth/quality |
|---|---|---|
| package `sse/` | SSE2 baseline; SSSE3 adds `pshufb` rotations; SSE4.1 load scheduling; AVX = 128-bit encodings only. Single-message, 2-lane. **No AVX2 → obsolete-good** | `neon/`: single-message, ARMv7+AArch64, `tbl`/`ext` rotations (correct technique); slower-than-scalar reports on some AArch64 cores — the 2-lane + strong-scalar problem, needs our measurement (U2) |
| libb2 | same lineage as package `sse/`, minus AVX2 (commented), plus racy install | none |
| libsodium | **best maintained x86 single-message set**: SSSE3/SSE4.1/AVX2 compress with proper byte-permute rotations, sound runtime picker, active fix history | none |
| blake2b-rs | package `sse/` verbatim (2020 copy) — inherits its ceiling | none |
| blake2 (RustCrypto) | effectively portable (legacy `simd*` features off-default, nightly-era) | none |
| blake2b_simd | SSE4.1 + AVX2, **including the only maintained 4-way interleaved `many` batch** — the strongest x86 story for the Equihash leaf shape; measured by us only on ARM so far | none (portable on ARM — measured 86 ns/leaf, our current leaf-shape best) |
| BLAKE3 C | SSE2/SSE4.1/AVX2/AVX-512, asm variants, dispatch — the structural gold standard | ✓ NEON kernel maintained (32-bit words vectorize well — different algorithm economics) |

## 3. Rust side (after the C core stands)

- `blake2ref` evolves into the wrapper over UniBlake — same opaque
  runtime-sized-state design, now with dispatch underneath.
- **Native Rust carries no as-is guarantee.** The current pieces
  (`Req/rust`'s bundled scalar, `blake2b_simd`, `blake3`) are today's
  state, not protected incumbents. The guiding principle is matching
  the UniBlake requirements — or a **declared subset**: a consumer
  states which requirements it targets (e.g., a verifier needs R2+R9
  and nothing else; a miner path needs R6 too), and implementations
  are judged against the subset they claim, explicitly, not against
  the full list by default. Whatever that implies is admissible —
  selection among existing crates, adoption, adaptation, or a
  rewrite — under the same §1a invariants (oracle gate + pinned
  provenance) in every case.
- **Mix/match already exists and is not rebuilt**: `Req/rust`'s Seam A
  (`hash/` candidates + equivalence gates + autodetect-behind-self-test)
  is the mix/match mechanism; UniBlake registers there as one more
  candidate. One new capability falls out: cross-language A/B under
  identical solver conditions (native Rust vs. C-dispatch backends in
  the same run).

## 4. Phases

| Phase | Content | Gate |
|---|---|---|
| U0 | Skeleton: `ub_` API, CMake (3 OS), streaming core + vendored-ref oracle backend, KAT/self-test harness (reuses RFC self-test + persona vectors), C-side bench harness on reqbench discipline | compiles+passes on macOS/arm64 and Linux; Windows CI-shaped |
| U1 | Dispatch: probe, table, registration, `UB_FORCE_IMPL`; pluggable scalar variants demonstrable (e.g. ref vs. an experimental unrolled scalar) | forced-impl benchmarking works; oracle self-test gates every variant |
| U2 | Specializations: NEON TU (measurable on this machine — re-homes icebox A13 with proper sequencing), SSE4.1/AVX2 TUs (compile-tested cross-target; performance validation needs real x86 — A7's condition) | each variant beats or ties portable on its hardware, else stays off |
| U3 | Batch/"many" API + concurrency hooks (jobs interface, documented threading boundary) | leaf-shape benchmark vs. `blake2b_simd::many` on x86 |
| U4 | Rust wrapper over UniBlake; register at Seam A behind the self-test gate | `all_hashers_agree`-class equivalence + bench |
| U5 | BLAKE3: vendor official C, same `ub_` API family | KATs + dispatch parity with the blake3 crate |

## 5. Sequencing honesty

U0–U1 are *infrastructure* (the same class as reqbench and Seam A —
built before campaigns, justified by making experiments cheap and
uniform). U2 tuning and U3 are *optimization-stage* and stay gated on
measurements — this proposal re-homes rather than reverses the A13
verdict. UniBlake is the **hash-track** focus; it must not displace
A5 (TMTO steepness) as the **solver-track** priority — hashing is ~17%
of solve time, the steepness science is the program's core question,
and the two tracks share no code. Run in parallel, solver track first
when forced to choose.

## 6. Consumers, eventually

`rk/original`, `cs`, `rz` cross-check binaries, RT reference builds
(all currently on the raw vendored files — they switch to UniBlake's
oracle backend or stay put; zero urgency), `Req/rust` Seam A (U4),
future Zebro Requihash solver (via the Rust side), Zero400 explicitly
untouched (libsodium is load-bearing there across four primitives).
