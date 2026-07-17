# Platforms — x86 SIMD instruction families, NEON, and what to expect

Reference for UniBlake's SIMD targeting decisions (`UniBlake.md` §1c,
§2b). Answers three practical questions:

1. what the SSE/AVX/… abbreviations mean,
2. what you can actually **expect to be present** on laptops and servers
   shipped from ~2020 onward,
3. what each means for **performance** and for **backward compatibility**
   (i.e. "if I build for AVX2, what won't run it").

**Authoritative sources** (this doc summarizes; these define):

- Intel® Intrinsics Guide — per-intrinsic, per-ISA:
  <https://www.intel.com/content/www/us/en/docs/intrinsics-guide/>
- Intel® 64 and IA-32 Architectures Software Developer's Manuals (SDM),
  esp. Vol. 1 ch. on SIMD and Vol. 2 (instruction set):
  <https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html>
- Intel® Architecture Instruction Set Extensions Programming Reference
  (future/optional extensions, incl. AVX10):
  <https://www.intel.com/content/www/us/en/developer/articles/technical/intel-architecture-instruction-set-extensions-programming-reference.html>
- Intel® ARK (per-CPU feature lookup): <https://ark.intel.com/>
- Arm: NEON is part of the AArch64 base ISA — Arm Architecture Reference
  Manual, and the Arm C Language Extensions (ACLE) for intrinsics:
  <https://developer.arm.com/architectures/instruction-sets/intrinsics/>

Everything below is background knowledge; verify a specific CPU on ARK.

## 1. The x86 SIMD families, in order

Each row is a distinct **feature flag** the CPU either has or doesn't;
a program using a family's instructions **faults (illegal instruction)**
on a CPU that lacks it. That is the whole backward-compatibility story:
newer families are supersets in spirit but must be **runtime-detected**
(via `cpuid`), never assumed.

| Family | Intro (Intel) | Vector width | What it added that matters for BLAKE2b |
|---|---|---|---|
| **MMX** | 1997 | 64-bit | integer only, aliases FP registers — obsolete, ignore |
| **SSE** | 1999 (P-III) | 128-bit | single-precision FP; `xmm` registers appear |
| **SSE2** | 2000 (P4) | 128-bit | **integer 128-bit** incl. 64-bit lanes (`__m128i`, `paddq`) — the floor for any 64-bit-word SIMD hash; **baseline of x86-64** (every 64-bit CPU has it) |
| **SSE3 / SSSE3** | 2004 / 2006 | 128-bit | SSSE3 adds `pshufb` (byte shuffle) — the key to fast BLAKE2b rotations (byte-permute instead of shift-or) |
| **SSE4.1 / 4.2** | 2007–2008 | 128-bit | better blends/loads; 4.2 adds CRC32/`pcmpistr` (not hash-relevant) |
| **AVX** | 2011 (Sandy Bridge) | 256-bit (FP) | 3-operand VEX encoding, 256-bit **float**; integer still 128-bit |
| **AVX2** | 2013 (Haswell) | **256-bit integer** | 256-bit `vpaddq`, `vpshufb`, gather — lets one instruction do **4×64-bit lanes**; the modern x86 target for BLAKE2b (2× the lanes of SSE) |
| **AVX-512** | 2016 servers (Skylake-SP); 2019+ some client | 512-bit | 8×64-bit lanes, mask registers; **fragmented** into sub-flags (F, BW, DQ, VL, …) and frequency/availability caveats — see §3 |
| **AVX10** | announced 2023, rolling out mid-decade | 256/512 | Intel's consolidation of AVX-512 into a versioned, more uniformly available ISA; **not something to target yet** |
| **SHA-NI** | 2016+ (client first) | — | hardware SHA-1/SHA-256 (not BLAKE2); irrelevant to us except as a "don't assume it" example |

Notes specific to BLAKE2b:
- BLAKE2b works on **64-bit words**, so the useful lane counts are
  **SSE2/SSE4.1 = 2 lanes**, **AVX2 = 4 lanes**, **AVX-512 = 8 lanes**.
- Rotation quality is the technique tell (`UniBlake.md` §2b): a good
  implementation does the 24/16-bit rotations with **byte-permute**
  (`pshufb`/`vpshufb`, needs SSSE3+/AVX2) rather than shift-or.

## 2. What to expect on 2020+ hardware

Short version: **SSE2 through AVX2 is a safe assumption on essentially
every x86-64 laptop and server from 2020 on. AVX-512 is NOT safe to
assume — and on much 2020+ client hardware it's absent by design.**

### Laptops / desktops (client), 2020 →

- **SSE2, SSE4.1, AVX, AVX2: present on virtually all.** AVX2 (2013) is
  ~a decade old; any Core i-series, Ryzen, etc. from 2020 has it.
- **AVX-512: mostly absent or disabled on client.** Intel enabled it on
  some 2019–2021 client parts (Ice/Tiger/Rocket Lake), then **removed it
  from 12th–14th-gen "hybrid" chips** (Alder/Raptor Lake): the efficiency
  (Gracemont) cores don't implement it, so — although the P-cores' units
  are physically present — Intel disabled it chip-wide, **fusing it off
  on Alder Lake silicon from early 2022 on** and closing the early
  enable-via-BIOS loophole through microcode/BIOS updates. AVX-512 is
  slated to return on later Intel client parts (Nova Lake era), but for
  2020+ hardware in the field it's effectively gone on Intel client. AMD
  added AVX-512 with **Zen 4 (2022)** and it's solid there. **Net: you
  cannot assume AVX-512 on a 2020+ laptop.**
- **Apple Silicon laptops (M1 2020 →): not x86 at all** — ARM/NEON (§5).
  A large and growing share of "2020+ laptops" run no x86 SIMD.

### Servers, 2020 →

- **SSE2–AVX2: universally present.**
- **AVX-512: common but not universal.** Intel Xeon Scalable
  (Skylake-SP 2017 → Sapphire/Emerald Rapids) generally has it, with
  varying sub-flags; AMD EPYC has it from **Zen 4 (Genoa, 2022)**. Older
  or lower-tier server/VPS instances, and many cloud VM shapes, expose
  only up to AVX2. **A cheap VPS in 2020+ reliably has AVX2, not
  necessarily AVX-512.**

### Intel generation → ISA map (the detailed timeline)

Confirmed from Intel's own documentation, not assumed — the single
reference for "which Intel silicon has what" (this table was previously
duplicated in `Req/BENCHMARK.md`; it now lives only here):

| Generation (Intel codename) | Year | SSE2/4.1/4.2 | AVX2 | AVX-512 |
|---|---|---|---|---|
| Pentium 4 onward | 2001+ | SSE2 yes | no | no |
| Nehalem (1st Core i-series) | 2008 | SSE4.1/4.2 yes | no | no |
| Haswell | 2013 | yes | **yes, first gen** | no |
| Skylake (client) | 2015 | yes | yes | no (client); Skylake-X/-SP (server/HEDT) added AVX-512, 2017 |
| Ice Lake (client + server) | 2019/2021 | yes | yes | **yes, both** |
| Alder/Raptor Lake (12th–14th gen, client) | 2021+ | yes | yes | **fused off chip-wide** — P-cores are AVX-512-capable silicon but Intel disabled it because the E-cores can't execute it and mixed P/E scheduling with an E-core-incompatible ISA is unworkable; early boards allowed re-enable via BIOS (disable E-cores), later steppings fused it off |
| Sapphire/Emerald/Granite Rapids (server, P-core Xeon) | 2023–2024 | yes | yes | **yes** — server P-core lines kept it |
| Sierra Forest (server, E-core-only Xeon) | 2024 | yes | yes | **no** — E-core Xeon skips AVX-512, relies on AVX10 |
| Nova Lake (client, upcoming) | ~2026 | yes | yes | **returning via AVX10.2** — first consumer line to unify P/E at 512-bit, ending the Alder-Lake fuse-off |

**Per-core, not just per-generation, is the real complication.** From
Alder Lake through pre-Nova-Lake, a "modern Intel laptop" is a hybrid
chip: P-cores are AVX-512-capable silicon Intel disabled at the chip
level *because* the E-cores can't execute AVX-512 and the OS scheduler
can't reliably pin AVX-512 threads to P-cores. This is a
scheduling/compatibility decision, not a P-core silicon limit — and a
runtime `is_x86_feature_detected!`/`cpuid` check correctly reports "no
AVX-512" (the bit is fused off), so no code needs special-casing. It is
exactly *why* **AVX2 is the realistic universal x86 SIMD floor** for a
consumer target, even though the same silicon generation has AVX-512
working on the *server* side.

### Practical targeting rule for UniBlake

- **Portable baseline**: plain C (`ref`) — runs everywhere, no SIMD flag.
- **x86 fast path worth building**: **AVX2** — the sweet spot; present on
  the overwhelming majority of 2020+ x86, 4×64-bit lanes.
- **SSE4.1 fallback**: for the rare 2020+ x86 without AVX2 (very old
  embedded/Atom); 2 lanes, still beats scalar on weak-scalar cores.
- **AVX-512**: treat as **opportunistic only** — detect at runtime, use
  if present, never require. Its fragmentation and client absence make
  it a poor build floor.
- All of this is **runtime-dispatched** (`UniBlake.md` §2, the probe in
  `uniblake/src/ub_probe.c`), which is precisely why the "what's present"
  question is answered per-CPU at load, not per-build.

## 3. Backward compatibility — the rules that bite

1. **Instructions fault if absent.** Run an AVX2 instruction on a
   pre-2013 (or AVX2-fused-off) CPU → `#UD` illegal-instruction crash.
   There is no graceful degradation in hardware; the software must not
   issue the instruction. Hence runtime `cpuid` detection before use.
2. **x86-64 guarantees SSE2 and nothing newer.** The 64-bit ABI baseline
   is SSE2. Everything above it is optional and must be detected.
3. **AVX/AVX-512 have a state cost.** Using them requires the OS to have
   enabled the wider register state (XSAVE / `XCR0`). Modern OSes do;
   the point is that "the CPU has AVX2" and "the OS lets me use it" are
   two checks (libraries like libsodium handle both).
4. **AVX-512 is not one thing.** It's a family of sub-flags (F, VL, BW,
   DQ, CD, …). "Has AVX-512" is meaningless without saying which subset;
   code must detect the specific sub-flag it uses.
5. **Downclocking (historical).** Early AVX-512 (and some AVX2) parts
   dropped clock frequency under heavy wide-vector use, sometimes making
   the wide path a net loss for short bursts. Mostly improved on recent
   silicon, but it's why "wider ISA" ≠ "always faster" and why UniBlake
   **measures before defaulting** (`UniBlake.md` §1c) rather than
   assuming AVX-512 > AVX2 > scalar.

## 4. What a "supports SSE/AVX/NEON/…" claim actually means — and how to check

A library's README listing a long row of ISA abbreviations
("SSE2/SSSE3/SSE4.1/AVX2/AVX-512/NEON!") tells you far less than it
looks like. The word "support" collapses **five distinct levels**, only
the last of which is what you actually care about for a hash on your
hardware:

| Level | Claim | What it really means | How to verify |
|---|---|---|---|
| **L0 Named** | "has an AVX2 file" | a source file *exists* with those intrinsics; may not even be in the build | grep the tree for the TU; check it's in the build system, not orphaned |
| **L1 Compiled-in** | "builds with AVX2" | the TU compiles for the target; the symbols are in the binary | `nm`/`objdump` the object/lib for the intrinsic instructions or the kernel symbol |
| **L2 Selectable** | "can use AVX2" | the code path can actually be *reached* — compile-time flag or runtime dispatch decides | is selection compile-time (a build flag, fragile) or runtime (a `cpuid` probe)? read the dispatch site |
| **L3 Selected here** | "uses AVX2 on this CPU" | on *your* machine, at load, that path is the one that runs | print the chosen backend at runtime; force each and confirm |
| **L4 Measured-better** | "AVX2 is faster here" | the selected path actually **beats** the alternatives on this workload/CPU | benchmark it against scalar and the other tiers; a long abbreviation list says nothing here |

The trap: marketing and even honest changelogs speak at **L0–L1**
("we support AVX-512!"), while the only levels that change your result
are **L3** (does it run on my box) and **L4** (does it help). The
uniblake NEON result is the cautionary case — L3-present and
L1/L2-correct, but **L4-negative** (slower than scalar on the M4). "NEON
support: yes" was true and useless; the measurement was the fact.

### How to check each level, concretely (uniblake tooling as the worked example)

- **L0/L1 — inspect the code.** Does the SIMD TU exist and is it built?
  ```sh
  grep -rl "vld1q_u64\|_mm256_\|_mm_shuffle" BLAKE/uniblake/src   # find intrinsic TUs
  grep -n "kernel_neon" BLAKE/uniblake/CMakeLists.txt              # is it in the build?
  nm BLAKE/uniblake/build/libuniblake.a | grep ub_compress         # which kernels linked in
  objdump -d <obj> | grep -iE "blr|vld1|vpshufb"                   # actual instructions emitted
  ```
  This is how you tell a *real* AVX2 path from a `#ifdef`'d-out stub or
  a scalar fallback wearing an AVX2 filename.
- **L2 — read the selection mechanism.** Compile-time (`build.rs`
  target features, `#ifdef __AVX2__`) means the fast path is baked at
  build time and **untestable on a build host that lacks the feature** —
  a real portability hazard (`blake2b-rs` and libb2 both do this;
  `UniBlake.md` §3 anti-patterns). Runtime dispatch (a `cpuid` probe
  installing a function pointer) means one binary adapts per CPU. Check
  which by reading the dispatch site (`uniblake/src/ub_core.c`
  `choose_kernel_from_cpu`, `ub_probe.c`).
- **L3 — ask the running program.** Don't infer; make it tell you.
  ```sh
  ./ub_test | grep -E "probe:|auto-selected"   # what the CPU has, and what was chosen
  UB_FORCE_IMPL=neon ./ub_test                 # force a path; if it can't, it isn't really there
  ```
  The `probe:` line is L3-present; `auto-selected` is L3-chosen. A claim
  that survives `UB_FORCE_IMPL=<x>` running and passing the gate is real
  at L3; one that errors was L0/L1 vapor.
- **L4 — measure it, per architecture.** Only a benchmark on the actual
  target answers "does this help." `uniblake/tests/ub_kbench.c` forces
  each kernel and reports ns/leaf + MiB/s; that is what demoted NEON
  from default on the M4. **Correctness at L3 (passes the oracle gate)
  and speed at L4 are independent** — NEON was correct *and* slower.
- **Correctness cross-check (orthogonal to all five levels).** A path
  that runs is not a path that's *right*. The fixed KAT (BLAKE2b-512
  "abc") must be byte-identical on every platform and every forced
  kernel; the oracle gate (`ub_selftest`) validates each kernel against
  the untouched reference before it's ever selected. "It ran and didn't
  crash" ≠ "it computed the right hash."

**Bottom line for reading anyone's SIMD claims (ours included):** treat
an abbreviation list as L0 until you've checked the build (L1), the
dispatch (L2), the running selection (L3), and — the only one that
decides whether to use it — a measurement on the target (L4). This repo
writes L3/L4 claims with the command to reproduce them for exactly this
reason.

## 5. NEON (AArch64) — the ARM side

- **NEON (Advanced SIMD) is part of the AArch64 base ISA**: every 64-bit
  ARM core (Apple Silicon, AWS Graviton, Ampere, phones, Raspberry Pi 4+)
  has it. **No runtime detection needed to USE it** — unlike x86, where
  even SSE4.1 must be probed. (32-bit ARM did need `getauxval(AT_HWCAP)`;
  AArch64 does not.)
- **Width: 128-bit = 2×64-bit lanes** for BLAKE2b. That is the ceiling —
  there is no 4-lane NEON for 64-bit words the way AVX2 gives on x86.
- **Performance caveat we MEASURED** (`uniblake/STATUS.md`, U2): on a
  strong-scalar core (Apple M4), 2-lane NEON BLAKE2b is **slower than
  scalar** (0.55–0.70×). The wide out-of-order scalar pipeline already
  saturates the dependency chain; 2 lanes don't help a single 128-byte
  block. NEON can still win on **weaker in-order ARM cores** and for
  **batched/multi-message** hashing — which is why UniBlake keeps it
  registered-but-not-defaulted rather than dropped.
- **This is a documented, known effect — not our artifact.** Third-party
  reports of NEON BLAKE2b at or below scalar on AArch64:
  - Crypto++ issue #367, "BLAKE2b NEON suffers poor performance on
    ARMv8" — *slower than scalar on Cortex-A57, but NOT on Cortex-A53*
    (same code, opposite result → microarchitecture decides):
    <https://github.com/weidai11/cryptopp/issues/367>
  - Dizzy Zone, "BLAKE2b performance on Apple Silicon" (2025):
    <https://dizzy.zone/2025/03/27/BLAKE2b-performance-on-Apple-Silicon/>
- **BLAKE3 is the informative contrast — better on NEON, but for a
  structural reason, and still CPU-dependent.** BLAKE3 uses 32-bit words
  and a tree structure, so NEON fills lanes with *independent chunks*
  rather than accelerating one serial 64-bit compress. Reported NEON
  gains: ~2.34× over portable on Apple Silicon
  ([Iroh/BLAKE3 blog](https://www.iroh.computer/blog/hashing-multiple-blobs-with-BLAKE3)),
  but only **+26% on Raspberry Pi 4**
  ([BLAKE3 #310](https://github.com/BLAKE3-team/BLAKE3/issues/310)) and
  **slower than scalar on Cortex-A9**
  ([BLAKE3 #403](https://github.com/BLAKE3-team/BLAKE3/issues/403)). So:
  *not* the same as BLAKE2b (the tree/32-bit design vectorizes where
  BLAKE2b's 2-lane 64-bit compress doesn't), yet the same lesson — the
  figure swings 2.34×→0.9× on core alone.
- Optional ARM crypto extensions (SHA1/SHA2/AES) exist and *are* probed
  (`getauxval` / `sysctlbyname`), but BLAKE2 isn't among them.

### Expert NEON reference implementations to study (verify correct ISA use)

*Local mirror pinned at Linux v6.12:
`~/Work/ZK/ZKs/BLAKE/kernel-neon-refs/` (blake2b-neon + chacha-neon core
`.S` + glue `.c`, with a provenance README). GPL — study reference only,
never built or vendored into uniblake.*

To check a NEON BLAKE2 kernel against how domain experts actually use
the instruction set, the highest-authority references are in the
**Linux kernel crypto tree**, written/reviewed by the people who
maintain ARM crypto for Linux — **Eric Biggers** (Google) and **Ard
Biesheuvel** (Linaro/Google, the ARM crypto maintainer). Two lineages,
both directly relevant:

1. **BLAKE2b NEON, in-kernel (the closest possible reference).**
   `arch/arm/crypto/blake2b-neon-core.S` + `blake2b-neon-glue.c`, by
   Eric Biggers, acked by Ard Biesheuvel (Dec 2020):
   <https://lore.kernel.org/linux-arm-kernel/20201215234708.105527-1-ebiggers@kernel.org/T/>
   — **Crucial detail for us**: this expert BLAKE2b NEON kernel targets
   **32-bit ARM**, deliberately, *not* AArch64. The experts put BLAKE2b
   NEON where scalar 64-bit is expensive (arm32); on arm64, where scalar
   is strong, they did not — which independently corroborates our M4
   measurement and the "2-lane vs. strong scalar" analysis above. If you
   want a *correct* NEON BLAKE2b to diff instruction choices against,
   this is it; if you want a *fast-on-arm64* one, the expert answer is
   "there isn't a compelling one, by design."

2. **ChaCha20 NEON, in-kernel (the closest *algorithmic cousin*).**
   `arch/arm64/crypto/chacha-neon-core.S`, by Ard Biesheuvel — a
   straight arm64/NEON port of the x86 SSE3 ChaCha, later extended to
   XChaCha by Eric Biggers:
   <https://patchwork.kernel.org/project/linux-arm-kernel/patch/1481294033-23508-3-git-send-email-ard.biesheuvel@linaro.org/>
   ChaCha20 is **BLAKE2's nearest relative**: BLAKE2's `G` mixing
   function is derived from ChaCha's quarter-round (same ARX
   add/rotate/xor structure, same 32-bit-word economics as BLAKE2s).
   ChaCha20 vectorizes *well* on arm64 (it's the Adiantum disk-encryption
   workhorse on ARM Android) precisely because it batches independent
   blocks — the same reason BLAKE3 wins and single-message BLAKE2b
   doesn't. Studying this kernel shows expert NEON ARX rotation idioms
   (`tbl`/`ext`/`rev`) and lane-batching structure to emulate.

Why these over a random GitHub port: they are **merged, reviewed,
regression-tested consensus code** maintained by the ARM crypto
maintainers, and they encode the experts' *judgment about when NEON is
worth it* (BLAKE2b→arm32 only; ChaCha→arm64 via batching), which is the
actual question, not just the intrinsics. Apple doesn't publish a
comparable open BLAKE2 NEON kernel; the kernel lineage above is the
authoritative open reference.

### Instructions to the implementer (specific to uniblake + the Requihash leaf)

If/when someone writes the next NEON (or x86) kernel here, this is the
concrete playbook for *our* situation — not generic SIMD advice.

**Our situation, stated plainly.** uniblake hashes the **Equihash leaf
shape**: the persona'd base state absorbs the serialized block-header
prefix and the nonce once, forming a shared midstate; then per leaf a
tiny counter tail is appended to a clone and finalized to a ~50-byte
digest. The marginal per-leaf cost is **≈1 compress of one 128-byte
block** — a *single-message, latency-bound* operation. This is the
worst case for 2-lane NEON on a strong-scalar core, and it is exactly
why the M4 measured a loss. **The kernel you write must match this
shape, or the number is a fiction.**

The exact sizes (from `Req/SPEC.md` §3–5; header sizes from Zero400's
`CBlockHeader`, `src/primitives/block.h`):

| Quantity | Value | Source |
|---|---|---|
| Persona (BLAKE2b `personal`) | 16 B = `"ReqhashPoW"`(10) ‖ `le32(n)`(4) ‖ `le16(k)`(2) | SPEC §3 — **encodes n,k**, not just ASCII |
| Header `input` | 108 B = version(4)+prevBlock(32)+merkleRoot(32)+saplingRoot(32)+time(4)+bits(4) | Zero400 `HEADER_SIZE` minus nonce |
| Nonce | 32 B (`uint256 nNonce`) | Zero400 |
| Prefix into S0 | **140 B** = input(108) ‖ nonce(32); `S0.update(input); S0.update(nonce)` | SPEC §3 |
| Iteration dial `m` (if ≥2) | `le16(m)` (2 B) absorbed after nonce | SPEC §3/§5 |
| Per-leaf tail | `le32(counter)` = **4 B** (`regular` keying adds `le32(class)`, 8 B total) | SPEC §4 |
| Digest length `hash_output` | `⌊512/n⌋·(n/8)` bytes | SPEC §5 |
| Leaf string | first `n/8` bytes of the digest | SPEC §4 |

Per-hash digest lengths across the parameter sets (the "50-byte-ish
neighborhood"), and the leaf-string length taken from each:

| (n,k) | `hash_output` (digest) | leaf `n/8` |
|---|---|---|
| (96,5) | 60 | 12 |
| (144,5) | 54 | 18 |
| (192,7) | 48 | 24 |
| (200,9) | 50 | 25 |

**Two truncations, at two layers** (why our stress test brackets ~50 B):
*64→`hash_output`* happens **in the hash primitive** — BLAKE2b always
computes 64 bytes internally and `final` copies only `outlen`
(=`hash_output`) out (`ub_final_with`'s `memcpy(out, buffer,
S->outlen)`; the Rust twin is `blake2b.rs:140`). *`hash_output`→`n/8`*
(e.g. 50→25) happens **in the solver**, taking the first `n` bits as the
consensus leaf string (`Req/rust/src/probe.rs`'s `d.truncate(n/8)`). The
hash primitive owns the first truncation via `outlen`; the leaf string
is the caller's slice of the digest.

**`m ≥ 2` (the iteration dial), briefly.** `m=1` is one hash per leaf
(the common case; midstate optimization applies). For `m ≥ 2`, S0 also
absorbs `le16(m)` (so different m give different base states), and each
leaf is hashed `m` times, re-binding the leaf's keying words each round
(`D_t = finalize(init(...).update(D_{t-1}).update(keying words))`),
which raises an attacker's per-recomputation cost — the honest solver
pays `m` once, a trade-off adversary pays `m` per recompute. Output size
is unchanged (`hash_output` per call); only the call count scales.

**Personalization lives in init, not in the compress kernel — but batch
APIs are where it gets dropped.** BLAKE2b personalization is xor'd into
the initial `h[]` by `init_param` *before* any compress runs; a
compress kernel (scalar or SIMD) only ever processes an already-
persona'd state, so a single-message NEON/AVX2 compress **cannot** lack
personalization (ours doesn't — the stress test proves persona'd output
matches the oracle). The trap is specific to **batch/`hash_many`
APIs**: some (e.g. historic `blake2b_simd::many` usage) interleave N
messages but assume a *single shared* parameter block, so a naive batch
kernel silently ignores per-message personas. For **us this is a
non-issue** — the leaf loop clones ONE persona'd `S0`, so all K batched
leaves share the same persona by construction. But if a future batch
API ever accepts per-message personas, the persona must be folded into
each lane's initial state, not assumed uniform. Verify with a batch
KAT that mixes personas across the lanes before trusting it.

**Do this:**

1. **Decide single-message vs. batch FIRST — it dominates everything.**
   The kernel lineage above teaches the real lesson: single-message
   BLAKE2b NEON is a dead end on arm64 (Biggers/Biesheuvel shipped it
   only on arm32); the wins come from **batching independent messages
   across lanes** (why ChaCha/Adiantum and BLAKE3 win on arm64). Our
   leaf loop is *embarrassingly batchable* — thousands of independent
   leaves share the prefix midstate. So the high-value kernel is **not**
   a faster single compress; it is a **`hash_many`-style batch that
   finalizes K leaves at once** (K = lane count), each = midstate-copy +
   counter append + one compress, interleaved across lanes. This is the
   R6 batch track (`UniBlake.md` §6a) — the design already flags it as
   deferred; a single-message NEON kernel (what U2 built) is the *floor*,
   the batch kernel is the *point*. Study `blake2b_simd::many` and
   Neves' `blake2-avx2` `blake2bip` for the interleave structure.
2. **Reuse the vendored round macros; do not hand-roll the mixing.** The
   NEON kernel here (`src/kernel_neon.c`) already includes the donor's
   `blake2b-round.h` unmodified (§1c single donor, provenance-pinned).
   Keep that: the G-function rotation idioms (`vrev64q` for rot32,
   `vext`-based rot24/rot16, `add+shr` for rot63) are the expert-correct
   ones — the same idioms the kernel ChaCha/BLAKE2b code uses. Writing
   your own rotate is how you get a subtly-wrong kernel that still passes
   short KATs. **Match the field names/order in `ub_state`** (already
   done — see the layout discussion in `uniblake/src/ub_internal.h`) so
   the vendored headers drop in.
3. **Keep transient SIMD state OFF the midstate struct.** The expanded
   message / lane working set is per-compress scratch → kernel locals
   (automatic storage), never fields in `ub_state`. The leaf loop copies
   the midstate per leaf (2^21 times at n=200); a bloated struct taxes
   every copy (`ub_internal.h` explains why additions append, not
   inline).
4. **Gate correctness before you ever look at speed.** Register the
   kernel and run it through `ub_kernel_matches_oracle` (the shared
   battery) + `ub_stress` (3021 checks: all block boundaries, the
   ~50-byte outlens, persona on/off, chunked-update). A SIMD kernel that
   passes `abc` but fails at a block boundary or a non-64 outlen is the
   normal failure mode — the stress battery is built to catch exactly
   that. **Never** promote on speed alone; correctness at L3 and speed
   at L4 (§4) are independent.
5. **Measure the leaf shape AND bulk, on the exact target, and report
   the CPU.** `ub_kbench` already does both and now prints the CPU
   identity (§6). A batch kernel can *lose* on the single-leaf number
   and *win* on aggregate throughput — measure the metric that matches
   how the miner actually calls it (many leaves), not a synthetic bulk
   loop alone.
6. **Only then update the win-list.** Auto-selection is a
   per-microarchitecture win-list in `choose_kernel_from_cpu()`
   (`ub_core.c`), keyed on `ub_cpu_info` (vendor + generation), *not* on
   ISA presence. Add an entry only for the specific core class where you
   measured a win. Apple M-series is explicitly *not* on it. Leaving a
   correct-but-slower kernel registered-and-forceable (as NEON is) is
   the right resting state — it is available to force, measure, and
   promote later without re-plumbing.

**Best practices in this area (the domain conventions the kernel code
embodies):**

- **Vendor the pinned bytes; don't chase upstream.** Consensus hashing
  needs a frozen, provenance-pinned kernel (§1a), not a moving
  dependency. The kernel crypto tree freezes its `.S` files for the same
  reason.
- **Assembly vs. intrinsics:** the experts write **hand assembly**
  (`.S`) for the hot kernel because compilers schedule NEON ARX poorly
  and intrinsics leave register allocation to the compiler. For uniblake
  the pragmatic order is: **intrinsics first** (portable, readable,
  what `kernel_neon.c` does), drop to assembly **only if a measured win
  justifies it** on a target that matters — never speculatively.
- **One kernel = one compress variant, oracle-gated** (§1b): don't fold
  parameter-block handling or finalization into the SIMD unit; keep the
  byte-shaping in the shared core so a wrong digest bisects to one TU.
- **Watch the tail/partial-block path.** Most SIMD hash bugs live in the
  last (padded) block and the counter/finalization flags, not the main
  loop — which is why the stress test hammers boundaries and the
  chunked-update seam specifically.
- **Alignment and endianness are load-bearing.** Use unaligned loads
  (`vld1q`) unless you can guarantee alignment; BLAKE2b is
  little-endian-defined, so byte order is fixed regardless of host — the
  oracle gate catches any slip.
- **Downclock/thermal awareness (x86 more than ARM).** On x86, a wide
  AVX-512 kernel can lower core frequency; measure sustained, not burst,
  and compare against the AVX2 tier not just scalar (§1, §6).

## 6. A SIMD number is meaningless without its exact machine — and benchmarks are gamed

Two cautions that gate how every performance figure in this repo (ours
included) should be read:

1. **No spec, no number.** "AVX2 is 2× faster" or "NEON is 0.7× scalar"
   is not a portable fact — it is a reading of one *(exact CPU
   model + stepping/microcode, frequency governor, thermal state, memory,
   compiler + flags, input shape)* tuple. The BLAKE2b/BLAKE3 NEON
   evidence above is the proof: the *same code* runs faster than scalar
   on one ARM core and slower on another (Cortex-A53 vs A57; M-series vs
   Cortex-A9). A result reported without its full machine spec is an
   anecdote, not a measurement. This repo therefore states the CPU and
   conditions with every figure and ships the harness (`ub_kbench`) so
   the reader re-measures on *their* box rather than trusting ours.
2. **Benchmarks get gamed from both ends.** People **code to
   benchmarks** (tune a kernel until the one microbench it's judged by
   improves, sometimes at the expense of real workloads), and vendors
   **design processors to benchmarks** (silicon and microcode optimized
   for the SPEC/Geekbench-shaped code everyone cites). The consequence:
   a headline SIMD speedup can reflect *how well the benchmark matches
   what got optimized* more than any intrinsic property of the
   algorithm. Defenses this repo uses: measure the **actual workload
   shape** (the Equihash leaf, not a synthetic bulk loop alone —
   `ub_kbench` reports both, and they disagree), keep the **scalar
   reference as the honest floor**, and treat any single number as
   provisional until it reproduces across machines. Wider ISA ≠ faster;
   faster-on-benchmark ≠ faster-in-use.

## 7. One-paragraph summary

For 2020+ machines: assume **SSE2→AVX2 on x86, NEON on ARM**; treat
**AVX-512 as opportunistic** (absent on much client hardware, fragmented
on servers) and **AVX10 as not-yet**. Build a portable scalar baseline
plus an **AVX2** x86 fast path and a **NEON** ARM path, select at runtime
by `cpuid`/base-ISA, and — because wider is not automatically faster
(downclock, lane-count vs. strong scalar) — **measure each path on real
hardware before making it the default**, exactly as UniBlake's §1c
requires and as the NEON result already demonstrated. And read every
resulting number with §6's two cautions in mind: it is only valid for
its exact machine, and benchmarks are gamed from both the code and the
silicon side.
