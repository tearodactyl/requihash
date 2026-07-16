# UniBlake — unified C/C++ BLAKE2 (and BLAKE3) setup

**Status: PROPOSAL, decision pending.** Design for discussion; nothing
below is built. On approval this becomes `BLAKE/uniblake/`, a PLAN
item, and the supersession target for `BLAKE.md` §5.4's dispatch
sketch and icebox A13.

**Document intent — written for later repartitioning.** This file is a
single home for the design *now*, but each numbered section is written
to stand alone so it can be split out later (e.g. a sourcing policy
doc, a validation-methodology doc, an anti-pattern watch-list, a
state-format spec). Cross-references are by section title, not just
number, to survive that redistribution.

## 1. What it is

One C-core library presenting one API across every target platform
(Linux/macOS/Windows; arm64/x86_64; C99 core with a C++14-friendly
header), containing:

- the **single-CPU reference path** as the always-present baseline,
  **carrying personalization from the start** (R2 includes R9, §2b) —
  the reference is not a stripped baseline to be enriched later; it is
  the validation/benchmarking oracle and must express the full
  Equihash-relevant parameter surface (`outlen` + `personal`) on day
  one. Today: the vendored `blake2b-ref.c` lineage; §1a governs
  whether it stays vendored, gets a conforming revamp, or both — with
  an untouched reference retained as oracle regardless;
- **runtime auto-detection and dispatch, with variant registration and
  a forced-implementation override built into the same layer** (R3
  includes R7, §2b): a function-pointer selection installed by a
  one-time CPU probe (`cpuid` / `getauxval(AT_HWCAP)` / `sysctlbyname`;
  AArch64 NEON needs no probe), where "which kernels exist to select
  among" and "pin one kernel by hand" are not a separate subsystem but
  the same registration table the probe writes into;
- **x86 (SSE4.1/AVX2) and NEON specializations** as separate
  translation units with distinct symbols, **each pinning its donor
  provenance** (R8 wraps R4 and R5, §2b) — no specialization enters the
  tree without its source commit and mode (§1a) recorded, so the
  provenance manifest is complete by construction rather than
  reconstructed afterward. Candidate donors are surveyed in §2b; §1c
  governs preferring a single donor and adopting incrementally;
- **an exportable snapshot of internal state** with a versioned,
  format-stable wire shape distinct from the live data structure (§4) —
  supporting the midstate pattern durably (export once, import
  repeatedly) rather than only the in-process struct-copy it is today;
- **structure for batch/concurrent use kept on the table but
  deferred** (R6, §2b and §6): the leaf-shape batch ("many") API and
  the threading boundary are the *last* substantial capability, opened
  only after validation, interoperability, benchmarking, and
  single-message optimization work has landed. Detailed review
  materials are prepared now (§6); the design decision is postponed;
- **pluggable variants including for the scalar reference itself**:
  registration + a forced-implementation override (`UB_FORCE_IMPL`,
  "force implementation": bypass CPU auto-detection and pin one named
  kernel — `ref`, `sse41`, `avx2`, `neon`, or an experimental variant —
  for the process; the benchmarking/reproduction tool, precedented by
  capability-mask overrides like `OPENSSL_ia32cap` and `GODEBUG=cpu.*`.
  A forced variant still passes the oracle self-test or hard-errors —
  justification in §5. Name negotiable.

## 1a. Code sourcing policy — vendoring is per-component, with a preference order

Whether C/C++ code is vendored, and to what extent, is **decided per
component, not by doctrine**. There is an explicit **preference order**
for where a component's starting code comes from:

1. **The reference implementation** — first choice when it fits.
2. **A major distribution** (libsodium, the BLAKE2 package's own SIMD
   TUs, BLAKE3's official C) — next, when the reference lacks a needed
   shape and the distribution is well-maintained.
3. **Any distribution with a unique feature or shape we specifically
   want** — last, and typically as a **copy/paste donor or an idea
   source** rather than a wholesale dependency (e.g. libb2's CC0
   `cpuid` skeleton, Neves' AVX2 batch technique).

Any component may still be **revamped or originated outright** for
**feature** reasons (batch API, experiment hooks — which no existing
code has, §2b), **conformance** reasons (modern argument order,
C99/C++14 cleanliness, warning-free on current toolchains), or
**performance** reasons (measured, per the bench gates). Two invariants
govern:

1. **Conformance anchor**: at least one untouched reference
   implementation always remains linked as the oracle backend, and
   every variant — vendored, revamped, or original — passes the
   validation gate against it (§1d defines what "validation" admits).
   Correctness never rests on code origin.
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

## 1b. On "no acceleration by library swapping" — a preference, not a strict rule

**Origin**: formulated 2026-07-16 in `BLAKE.md` §0's requirements list
(assistant's derivation from the uniformity directive, not a verbatim
owner requirement); **downgraded here on instruction** from a rule to a
**default preference**.

**We integrate, validate, and optimize opportunistically.** The
posture is: acceleration is *preferably* added within the primitive as
kernel variants behind one core's selection, because that keeps the
audit increment small and the gate cheap (reasons below). But this is
not an invariant — where a whole-implementation integration is the
pragmatic path to a validated, measured win, it is admissible, taken
opportunistically, and recorded under §1a's provenance manifest like
anything else. The two-layer picture still describes the *default*:

- *Inside* UniBlake — the preferred shape is one core with
  selectable kernels, oracle-gated, over library facades, for these
  concrete reasons:
  1. **Audit increment**: a kernel variant changes only the compress
     step; parameter-block handling, state layout, and finalization —
     the byte-shaping, consensus-relevant code — stay fixed. A
     whole-library swap changes all of it at once (zcashd swapped
     BLAKE2b backends four times, 2016–2022 — `../SOLVERS.md` §3 —
     each a full provenance discontinuity).
  2. **Gate cheapness**: same-core kernel variants self-test against
     the in-tree oracle at startup for free; a library swap needs a
     full-surface equivalence campaign.
  3. **Bisection**: a wrong digest or a regression isolates to one
     kernel translation unit.
  4. **The anti-patterns are on record** (§3): `blake2b-rs`'s
     `build.rs` swaps whole C files at compile time behind frozen
     bindgen mirrors; libb2 stalled at AVX1 partly because variants
     weren't independently pluggable units.
- *At consumer seams* (Req Seam A and equivalents) — independent
  implementations, UniBlake itself included, remain swappable measured
  candidates behind equivalence gates. This was always the case and is
  unchanged.

The difference from the earlier framing: none of the above is stated
as a prohibition. It is the reasoning for a default that a compelling,
validated, measured case can override.

## 1c. SIMD sourcing — single-donor preference, incremental adoption

**Prefer x86 support from a single source** (§2b's survey points at
libsodium's SSSE3/SSE4.1/AVX2 compress TUs as the strongest maintained
single-message x86 set) **unless compelling reasons advise otherwise —
and even then, incrementally**: bring one ISA tier in, validate and
measure it, before the next. The same discipline applies to **NEON**
(the BLAKE2 package's `neon/` TU is the single obvious donor; adopt one
tier, validate, measure, then consider more).

Rationale: a single donor per SIMD family keeps provenance narrow (one
commit to pin per family, §1a) and technique consistent (§2b's
rot24/rot16 quality tell applies uniformly), and incremental adoption
means every added tier is separately validated and separately measured
— no batch of unvalidated SIMD lands at once. Mixing donors across
tiers within one family is the compelling-reason exception, taken
tier-by-tier, not the default.

## 1d. Validation methodology — three admissible oracle types

Validation of any kernel or variant may rest on **any of** the
following, and the methodology names which one a given check uses:

1. **A definitive reference** — side-by-side byte agreement against an
   untouched reference implementation linked as the oracle backend
   (the §1a conformance anchor). Strongest; always available in-tree.
2. **Published/generated vectors** — KATs (RFC 7693 self-test, the
   published "abc" vector, CPython `hashlib` personalization vectors,
   or vectors generated once from a trusted reference and committed).
3. **Operational validation** — agreement observed through a real
   consumer path (e.g. a solver's cross-check binary producing
   identical solutions, `all_hashers_agree`-class equivalence at Seam
   A). Weaker as a unit oracle but catches integration-level drift the
   other two can miss.

These are complementary, not ranked substitutes: a new AVX2 kernel
should pass (1) as a unit and ideally show (3) in at least one consumer
before it is trusted on the hot path.

### Change classification — build/platform vs. algorithmic

Every change to a vendored or derived component is classified as one
of two categories, and the category sets the scrutiny:

- **Build / platform-compatibility change** — argument-order glue,
  include-shim, `#ifdef` for a toolchain, replacing an x86-only
  intrinsic path with the portable one, CMake/linkage. These do **not**
  touch the algorithm's operations or their order. Lower scrutiny:
  the oracle gate (§1d types 1–2) is sufficient, because a correct
  build change is byte-invariant by definition.
- **Algorithmic change** — rearranging or rewriting the core
  operations or steps (compress rounds, G-function, message schedule,
  parameter-block construction, finalization). Highest scrutiny: full
  oracle agreement *plus* a recorded deviation list (§1a "rewritten
  from" mode) *plus*, for anything on a measured hot path, a
  re-benchmark.

This distinction is load-bearing: most of what UniBlake does to
third-party code is the first category (the whole `rk/original`,
RT-native, and vendored-header work to date has been build/platform
compatibility, byte-invariant), and conflating it with the second
would impose needless ceremony on portability work while under-
scrutinizing genuine algorithm edits.

## 2. Pluggability granularity — compress-first, not compress-only

**By design, pluggability is not restricted to the compress function.**
The prior record (`BLAKE.md` §5.4) already left this open — it speaks
of "one indirect call per compress *or batch*," i.e. the selectable
unit was never fixed at the compress function alone. A batch/"many"
kernel, or a whole streaming path specialized for a shape, are
legitimate future pluggable units.

**But the implementation starts with the compress function as the
initial pluggable unit.** One streaming core
(`init_param`/`update`/`final`/state-copy, ~100 lines, derived from the
reference with attribution) calls `compress(state, blocks, n)` through
the selection mechanism; the first variants are compress
implementations. This is the proven shape (libsodium's picker, libb2's
`--enable-fat`, BLAKE3's `blake3_dispatch.c`) and the cheapest first
target; broader granularity (batch kernels, §6) is added later without
re-architecting, because the selection layer is not conceptually
tied to the compress boundary.

**Open concern to review, not yet resolved — exclusive dispatch-table
selection may block compiler optimization.** Committing to *every*
kernel call going through a function-pointer table has a cost: it can
prevent inlining of the compress step into the streaming loop, defeat
LTO across that boundary, and block constant-propagation of
round/block counts. For the always-portable reference build, and for a
build pinned to one known kernel, a compile-time/`static` dispatch path
that inlines the chosen kernel may matter more than runtime
flexibility. **This pass only flags the concern**: the exclusive-table
commitment needs review before U1 hardens, and the design should not
foreclose a coexisting compile-time selection path. Escape-hatch
design is deferred.

The pristine vendored reference additionally stays linked as an
*oracle backend* (whole-implementation, no dispatch) so every variant
is validated against untouched reference bytes at startup (§1d,
§5) — this is independent of the granularity question.

## 2a. BLAKE3 — shape examined and specified now, integration deferred

**BLAKE3 is future adoption, not current work.** No active integration
happens in this pass. What happens now is **specifying the shape** so
the evolving BLAKE2 implementation accommodates BLAKE3 naturally when
the time comes:

- BLAKE3's official C **already is UniBlake-shaped** —
  `blake3_dispatch.c`, portable + SSE2/SSE4.1/AVX2/AVX-512/NEON
  translation units, runtime detection. Adoption ≈ vendor the official
  C the way BLAKE2 is vendored and present it through the same `ub_`
  API family; new construction ≈ zero.
- **Shape requirements the BLAKE2 core should honor now, to include
  BLAKE3 naturally later**: (a) the `ub_` API surface should not bake
  in BLAKE2's parameter-block model as the only keying/personalization
  path — BLAKE3 uses a derive-key / keyed-hash model instead, so the
  API's keying abstraction should be expressible by both; (b) the
  selection/registration layer (§2) should not assume a single
  compress signature — BLAKE3's compression is tree/chunk-shaped, not
  the BLAKE2 block-streaming shape, so "a kernel" must be able to mean
  a chunk-compression unit too; (c) the state-snapshot format (§4)
  should version the *hash family* in its header so a BLAKE2 snapshot
  and a BLAKE3 snapshot are distinguishable and neither constrains the
  other's internal structure.

These are **design accommodations, not integration tasks** — recorded
so U0/U1 don't paint BLAKE3 into a corner. Active BLAKE3 work stays
sequenced to the final phase (§6 phase table).

## 2b. The field vs. UniBlake's requirements (surveyed/verified 2026-07-16)

Requirements shorthand, **as re-annotated per the R-in-R directives**:

- **R1** unified build on Linux/macOS/Windows × arm64/x86_64
- **R2 (⊇ R9)** always-present scalar reference path **carrying
  personalization from the start** — the reference is the
  validation/benchmarking oracle; personalization is not deferred
- **R3 (⊇ R7)** runtime detection + dispatch **with variant
  registration and forced-impl override in the same layer**
- **R4** modern x86 SIMD — **provenance-pinned (R8), single-donor
  preferred, incremental (§1c)**
- **R5** NEON — **provenance-pinned (R8), single-donor preferred,
  incremental (§1c)**
- **R6** batch/"many" API + copyable states — **the final substantial
  capability; deferred behind validation/interop/bench/optimization
  (§6). Review materials prepared now, decision postponed**
- **R8** pinnable single provenance for this tree — **wraps R4/R5;
  every specialization pins its donor by construction**
- **R9** personalization — **folded into R2** (no longer a standalone
  late column)

| Implementation | R1 | R2⊇R9 | R3⊇R7 | R4 | R5 | R6 | R8 |
|---|---|---|---|---|---|---|---|
| BLAKE2/BLAKE2 package | ✗ source drop, no build system | ✓ (persona in ref) | ✗ compile-time only; no registration | SSE2–AVX/XOP; **no AVX2** | ✓ `neon/` (single-msg) | ✗ (states copyable; no batch) | **✓ (our vendor)** |
| libb2 | ✗ autotools rot (#40), no MSVC | ✓ | x86-only, **race #39**, arm64-macOS detect broken #36; no registration | SSE2–AVX/XOP; AVX2 commented out | ✗ | ✗ | adds a 2nd copy |
| libsodium | ✓ | ✓ (`_salt_personal`) | **✓ sound runtime picker**; no user registration | SSSE3/SSE4.1/**AVX2** compress TUs (verified) | ✗ | ✗ batch; state copyable | whole crypto lib for one primitive |
| blake2b-rs (Nervos) | cc-based; plausible MSVC | ✓ full builder | ✗ compile-time (target features) | package `sse/` lineage (no AVX2) | ✗ | ✗ | ✗ own unversioned 2020 copies |
| blake2 (RustCrypto) | ✓ (pure Rust) | ✓ (verified) | n/a | legacy off-default features only | ✗ | ✗ batch; states clonable | crates.io pin (Rust-only provenance) |
| blake2b_simd | ✓ (pure Rust) | ✓ | ✓ (x86 only) | SSE4.1 + **AVX2 incl. 4-way `many`** | ✗ | ✓ `many::hash_many` (pays only on x86) | crates.io pin (Rust-only provenance) |
| OpenSSL EVP | ✓ | **✗ no persona — disqualified** | n/a (portable only) | ✗ | ✗ | ✗ | heavy |
| *BLAKE3 official C (structural benchmark; different hash)* | ✓ | n/a (derive-key model) | ✓ `blake3_dispatch.c` | SSE2→AVX-512 | ✓ | tree-parallel inherent | vendorable |

**The finding that justifies UniBlake**: for BLAKE2b, the intersection
R3∧R4∧R5 (runtime dispatch + modern x86 + NEON) is **empty** across the
entire field — libsodium lacks NEON, the package's NEON lacks dispatch
and AVX2, `blake2b_simd` lacks NEON, libb2 lacks all three in practice.
R7 (registration/experiment hooks, now folded into R3) exists nowhere.
UniBlake is not reinventing an existing product; it is filling a
verified hole, with per-axis donors chosen under §1c's single-donor
preference: libsodium's AVX2 compress TUs (x86 donor), the package's
`neon/` TU (NEON donor), libb2 as the cautionary tale plus CC0 `cpuid`
skeleton. Batch donors (`blake2b_simd`'s `many`, Neves' `blake2-avx2`)
are catalogued for R6 but not adopted this pass.

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

## 2c. Prior art: libb2, examined (2026-07-16)

libb2 is the official org's own attempt at exactly this library — and
it validates the design (compress-level runtime dispatch over the
reference lineage, CC0, same code vintage as the reference repo: its
2023 sync carries the key-length fix). It is *not* the base to build
on, for verified reasons from its own source and tracker (the full
anti-pattern watch-list is §3): dispatch and acceleration are x86-only
(no NEON files exist; `AVX2` is literally commented out in
`blake2-dispatch.c`'s variant enum, so its best path is AVX1-era); the
fat-mode function-pointer install has an open data race (#39, 2022);
configure-time detection breaks on arm64 macOS (#36 — this machine's
class); autotools bit-rot breaks modern installs (#40) with CMake/Meson
requests unanswered (#37/#38); no release since 0.98.1, so distributions
package behind HEAD; and its simple API keeps the legacy 2016 argument
order (#47) — the §3.3 trap, institutionalized. UniBlake is, in one
sentence, *libb2 done for 2026*: ARM included, AVX2 included, race-free
one-time dispatch install, CMake, and (later) batch API and experiment
hooks. Its CC0 `cpuid` and dispatch skeleton are directly minable (§1a
tier 3 / copy-paste donor).

## 3. Anti-pattern watch-list — blake2b-rs and libb2

Concrete failure modes to design against, extracted from the two
closest prior products. Each is a thing UniBlake must not reproduce.

### From `blake2b-rs` (Nervos/CKB)

1. **Frozen bindgen struct mirrors.** It mirrors `blake2b_state` /
   `blake2b_param` as `#[repr(C)]` Rust structs generated once by
   bindgen (2020). If the C is ever updated without regenerating, the
   Rust view of the layout silently desynchronizes → memory
   corruption with no error. **UniBlake defense**: opaque,
   runtime-sized state; the wrapper never mirrors a C layout (this is
   already `blake2ref`'s design — `vendor/blake2-rs/README.md`).
2. **Unversioned vendored copies.** It vendors its *own* `ref/` and
   `sse/` from the package, commit unrecorded, 2020-era — no provenance
   path. **UniBlake defense**: §1a provenance manifest, every input
   pinned by commit and mode.
3. **Compile-time-only backend swap behind a build script.** `build.rs`
   picks `sse` vs `ref` at compile time from target features — no
   runtime dispatch, and the swap is a whole-file substitution, not a
   registered kernel. Wrong-target binaries silently run the slow path;
   the fast path is untestable on the build host. **UniBlake defense**:
   runtime registration + dispatch (R3), with `UB_FORCE_IMPL` making
   every kernel exercisable on any host.
4. **Dormancy on a consensus-critical path.** Unmaintained since
   2020-07 while used in CKB consensus hashing. **UniBlake defense**:
   in-tree, first-party, pinned — not dependent on an external
   maintainer's release cadence.

### From libb2

5. **Racy fat-mode dispatch install.** The `--enable-fat` function-
   pointer table has an open data race (#39, 2022) in its one-time
   install. **UniBlake defense**: race-free one-time initialization
   (a guarded `init`, or install-before-publish ordering) as an
   explicit U1 gate, not an afterthought.
6. **x86-only dispatch masquerading as portable acceleration.** Its
   runtime selection covers SSE2→AVX only, AVX2 commented out, no NEON
   — so on arm64 (and for AVX2 x86) it silently gives you the portable
   path while presenting a "fat" build. **UniBlake defense**: NEON is a
   first-class dispatch target (R5), and the build reports which
   kernels are actually compiled in, not just "fat".
7. **Configure-time host detection that breaks off the beaten path.**
   arm64-macOS detection is broken (#36); the build guesses from the
   *build host* and mis-selects. **UniBlake defense**: detection is a
   *runtime* CPU probe, not a configure-time host guess; the build
   compiles all applicable kernels and selects at load.
8. **Autotools/packaging bit-rot → distros ship stale code.** No
   release since 0.98.1 (#40, no CMake/MSVC — #37/#38), so packaged
   libb2 lags HEAD. **UniBlake defense**: CMake, 3-OS from the start
   (U0), vendored-not-system so consumers pin our tree, not a distro's.
9. **Institutionalized legacy argument order.** The simple API keeps
   the 2016 `blake2b(out, in, key, …)` order (#47) — `BLAKE.md` §3.3's
   trap, now baked into a shipped API. **UniBlake defense**: modern
   argument order in the `ub_` API, the 2016 order never re-exposed.

## 4. State snapshot: exportable format vs. live structure

**Design an export of a snapshot of internal state; plan for repeated
import.** The requirement is the midstate pattern made durable: today
the midstate is an in-process struct copy (`BLAKE.md` §5.1 — hash the
140-byte prefix once, copy the state, append per-leaf, finalize). The
generalization is a **snapshot you can export, persist if wanted, and
import repeatedly** to re-seed many finalizations.

**The load-bearing distinction: snapshot shape ≠ live data structure.**

- **The live internal state** is a dynamically-updated data structure
  free to change with the implementation — its field layout, alignment,
  any acceleration-specific scratch, and its evolution across kernels
  are internal. Nothing external depends on it. (This is exactly why
  the wrapper state is opaque, §3 defense 1.)
- **The exported snapshot** has a **versioned, format-stable wire
  shape** with **historical-format expectations**: a header (magic +
  format version + hash family, per §2a accommodation (c) so BLAKE2 and
  BLAKE3 snapshots are distinguishable) followed by the canonical
  chaining state, `t`/`f` counters, buffered-bytes, and the parameter
  block digest — the *mathematical* midstate, not the *in-memory*
  struct. Old snapshot versions must remain importable (or be
  explicitly, loudly rejected with a version error — never silently
  misread).

Import is the inverse: validate header/version, reconstruct a live
internal state from the canonical fields (regardless of which kernel
will run it), and proceed. This decouples "can I resume this midstate"
from "which build/kernel/version produced it" — the snapshot is
portable across kernels and, within version rules, across releases;
the live structure never has to be.

*(This is design intent, not yet a byte-layout spec. The concrete
header/field encoding is a U-phase deliverable, gated like the rest.)*

## 5. Justifying the forced-impl self-test gate

`UB_FORCE_IMPL` selects a kernel but **does not** bypass the oracle
self-test — a forced variant still passes validation (§1d type 1
against the linked reference oracle) or hard-errors at install.
Justification, since forcing is precisely the "I know what I want"
override:

1. **Forcing is a selection tool, not a trust escalation.** The whole
   value of `UB_FORCE_IMPL` is reproducible benchmarking and
   bisection — pinning a *known-correct* kernel to measure or isolate
   it. A forced kernel that is silently wrong produces confidently
   wrong measurements and a false bisection result, defeating the
   tool's own purpose.
2. **The gate is nearly free** (§1b reason 2): same-core kernel
   variants self-test against the in-tree oracle at startup at
   negligible cost. There is no performance argument for skipping it,
   so the only thing skipping would buy is the ability to run a
   broken kernel — which no legitimate use wants.
3. **It preserves the one invariant that survives the §1b downgrade.**
   §1b relaxed "never swap libraries" to a preference, but the
   conformance anchor (§1a invariant 1) is *not* relaxed: correctness
   never rests on code origin *or* on operator intent. "I forced it"
   is operator intent; the gate is what keeps intent from overriding
   correctness. Precedent overrides (`OPENSSL_ia32cap`, `GODEBUG=cpu.*`)
   likewise select capability, they don't disable the library's
   internal correctness checks.

Forcing selects; it never bypasses the gate.

## 6. Phases, sequencing, and the deferred batch/concurrency track

### Sequencing honesty

The early phases are *infrastructure* (the same class as reqbench and
Seam A — built before campaigns, justified by making experiments cheap
and uniform). SIMD tuning is *optimization-stage* and stays gated on
measurements — this proposal re-homes rather than reverses the A13
verdict. UniBlake is the **hash-track** focus; it must not displace
A5 (TMTO steepness) as the **solver-track** priority — hashing is ~17%
of solve time, the steepness science is the program's core question,
and the two tracks share no code. Run in parallel, solver track first
when forced to choose.

### Phase table

R6 (batch/"many") is an **explicit phase but not guaranteed to be the
last** — it opens only after the validation/interoperability/
benchmarking/single-message-optimization milestone, but work may still
follow it. BLAKE3 integration is likewise late and gated.

| Phase | Content | Gate |
|---|---|---|
| U0 | Skeleton: `ub_` API (persona-carrying reference = R2⊇R9), CMake (3 OS), streaming core + vendored-ref oracle backend, validation harness (all three §1d oracle types wired: reference-agreement, RFC self-test + persona vectors, an operational check) , C-side bench harness on reqbench discipline | compiles+passes on macOS/arm64 and Linux; Windows CI-shaped; all three validation oracle types green |
| U1 | Dispatch + registration + forced-impl (R3⊇R7): probe, table, registration, `UB_FORCE_IMPL`; pluggable **scalar** variants demonstrable (ref vs. an experimental unrolled scalar). **Exclusive-dispatch-table optimization concern (§2) reviewed here** before hardening | forced-impl benchmarking works; oracle self-test gates every variant (§5); dispatch-table optimization concern resolved or a coexisting compile-time path specified |
| U2 | SIMD specializations, single-donor + incremental (§1c), each provenance-pinned (R8): NEON TU (measurable on this machine — re-homes icebox A13 with proper sequencing), SSE4.1/AVX2 TUs (compile-tested cross-target; performance validation needs real x86 — A7's condition) | each variant validated (§1d) and beats-or-ties portable on its hardware, else stays off; donor pinned |
| U3 | State-snapshot export/import (§4): versioned wire format distinct from live structure, repeated-import path, cross-kernel/cross-version import rules | round-trip + cross-kernel import validated; old-version import either works or errors loudly |
| **U4** | **Batch/"many" API + concurrency hooks (R6) — the deferred track.** Materials prepared in §6a now; **decision and design postponed** until U0–U3's validation/interop/bench/optimization has landed. Not guaranteed terminal | opens only past the validation/interop/bench milestone; leaf-shape benchmark vs. `blake2b_simd::many` on x86 |
| U5 | Rust wrapper over UniBlake; register at Seam A behind the self-test gate | `all_hashers_agree`-class equivalence + bench |
| U6 | BLAKE3 adoption: vendor official C, same `ub_` API family (shape already accommodated per §2a) | KATs + dispatch parity with the blake3 crate |

### 6a. Batch / concurrency / threading — review materials, discussion postponed

**On instruction: prepared for review now, decision deferred.** The
options and their costs, so the eventual discussion starts from a map,
not a blank page:

- **Batch ("many") API.** Interleave N independent messages through
  one compress call to fill SIMD lanes. Donor shapes: `blake2b_simd`'s
  `many::hash_many` (4-way AVX2, measured *counterproductive on ARM* —
  `vendor/blake2-rs/README.md`), Neves' `blake2-avx2`. Value is
  x86-only for BLAKE2b (2-lane NEON doesn't win); BLAKE3's tree
  structure batches inherently. **Open question**: is the Equihash
  leaf-generation shape (many tiny messages sharing a 140-byte prefix)
  better served by a batch API or by the existing midstate + snapshot
  (§4) path? Unmeasured on x86.
- **Copyable reentrant states.** Already the midstate mechanism;
  §4's snapshot generalizes it. Thread-safe *by value* — each thread
  owns its state copy.
- **Threading boundary.** Hashing is embarrassingly parallel *across
  messages*; the standing position is that UniBlake provides batch
  interfaces and by-value states while *scheduling* (thread pools,
  rayon, pthreads) stays in consumers. Whether UniBlake should offer
  any built-in job interface at all is the postponed question.

No decision is taken here. These materials exist so U4's discussion is
short.

## 7. Rust side (after the C core stands)

- `blake2ref` evolves into the wrapper over UniBlake — same opaque
  runtime-sized-state design (§3 defense 1), now with dispatch
  underneath.
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

## 8. Consumers, eventually

`rk/original`, `cs`, `rz` cross-check binaries, RT reference builds
(all currently on the raw vendored files — they switch to UniBlake's
oracle backend or stay put; zero urgency), `Req/rust` Seam A (U5),
future Zebro Requihash solver (via the Rust side), Zero400 explicitly
untouched (libsodium is load-bearing there across four primitives).
