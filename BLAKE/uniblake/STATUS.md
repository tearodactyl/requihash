# uniblake — PoC status & findings

**Result: GREEN on arm64 macOS (M4).** The UniBlake shape is proven
runnable, not just designed. Built with Apple clang 21 / CMake 4.2;
**all six ctests pass** (validation, force-ref, force-unrolled, stress,
snapshot, gate-rejection). Beyond the C0–C3 PoC core, this tree now
also realizes **U2** (a real NEON kernel — correct, measured) and
**U3** (versioned state snapshot).

## What the PoC proves (checkpoints C0–C3)

| Claim | Evidence |
|---|---|
| Persona-carrying reference API (R2⊇R9) | `ub_blake2b_init_personal`; the operational midstate test hashes an Equihash-shaped persona through 2048 leaves, all matching the oracle |
| Runtime CPU probe on M4 (R3) | `ub_detect_cpu()` calls `sysctlbyname`; reports `neon=1` on this machine |
| Registration + dispatch + `UB_FORCE_IMPL` (R3⊇R7) | two kernels registered; auto-selection resolves to a concrete kernel; `UB_FORCE_IMPL=ref_unrolled` observed to switch the active kernel |
| Oracle self-test gate (§5) — positive | both `ref` and `ref_unrolled` pass the byte-for-byte battery vs. the untouched vendored reference |
| Oracle self-test gate (§5) — **negative** | the deliberately-broken kernel is **rejected** by the same criterion (`ub_gate_test`: "broken kernel is rejected"); makes "forcing selects, never bypasses the gate" observable |
| All three §1d validation oracle types wired | published `abc` KAT (type 2), reference-agreement battery (type 1), operational midstate path (type 3) — all green |
| Opaque state, no mirrored layout (anti-pattern #1) | `ub_state_size()`/`ub_state_align()` report at runtime (240/8 here); public header keeps `struct ub_state` incomplete |
| Compress-first pluggable seam (§2) | kernels are `ub_compress_fn(state, blocks, nblocks)`; the core calls through a selected pointer |

**Validation coverage vs. the BLAKE2/BLAKE3 reference suites.** Our
`ub_stress` (3021 checks/kernel) and the reference suites are
complementary in shape: the **BLAKE2 reference self-test**
(`blake2b-ref.c` under `BLAKE2B_SELFTEST`) is the gold standard for the
*streaming seam* — it replays the keyed KAT while varying the update
chunk size `step` from 1 to 127 across every KAT length (≈`KAT_LENGTH ×
127` streaming permutations); **BLAKE3's official vectors** are 35 fixed
one-shot cases spanning input lengths 0…102400 (chunk boundaries at
1024). Ours adds what neither does *for our situation*: **the
personalization axis** (persona on/off — BLAKE2's KAT is keyed, not
personalized), the **~50-byte outlen neighborhood** (both references
test one output length; we test the Requihash digest sizes), and the
**cross-kernel differential** (every registered kernel vs. the oracle,
not one implementation vs. fixed bytes). We do borrow the two reference
ideas that matter: the BLAKE2 self-test's random-chunk streaming
(our chunked-update case) and its all-boundary length sweep (our 0…300
loop). Net: ours is narrower on raw KAT breadth (we validate against a
live oracle, not a frozen vector file) but broader on the axes the
Requihash use actually exercises.

## What the PoC deliberately does NOT do

- **NEON kernel present but not defaulted** (U2 done) — `neon` is
  registered, stress-proven, and forceable, but measured slower than
  scalar on the M4 so auto stays on `ref` (see finding 3).
- **State snapshot present** (U3 done) — versioned export/import
  (`ub_blake2b_export`/`import`, `src/ub_snapshot.c`), distinct from the
  live-struct `ub_blake2b_copy`. Validated: round-trip, 5000 repeated
  imports, cross-kernel, loud version/family/corruption rejection.
- **No x86 SIMD kernels** — x86 AVX2/SSE4.1 kernels (U2 continued) not
  written; need real x86 to validate/measure (A7).
- **No x86/Windows run** — those code paths are written and
  guard-balanced but unrun this session (no hardware). `BUILD.md`
  grades them STRUCTURED, NOT YET RUN, with verbatim directions.
- **No batch/"many"** — R6, deferred (§6a materials only).
- **No Rust wrapper / BLAKE3** — U5 / U6.

## Findings worth carrying forward

1. **Dispatch tax is negligible — MEASURED (U1).** The §2 concern
   (exclusive function-pointer dispatch blocking inlining) was
   quantified with `tests/ub_bench.c`, isolating the one variable:
   direct `ub_compress_ref(...)` vs. an indirect call through a
   runtime-loaded pointer (laundered through `volatile` to defeat
   devirtualization — the real shipped situation). On arm64 M4, -O2:
   **~0.25–0.68% per compress (~0.3–0.7 ns on a ~106 ns compress).**
   The indirect `blr` is well-predicted; the 12-round compress body
   dominates. **Conclusion: the exclusive dispatch table is fine on
   this leaf shape; no coexisting compile-time-inlined path is needed
   for performance.** The §2 concern is resolved for scalar kernels; if
   a future SIMD kernel makes compress cheap enough that ~0.5 ns
   matters, re-measure then — but do not pre-optimize now.
   - *Method note — the "reverse −15%", an instructive accidental
     finding*: an earlier bench compared two full streaming paths and
     reported dispatch as **15% *faster*** — a physically impossible
     negative cost. Root cause: the two paths weren't equal work. The
     "inlined" side was hand-written `static` wrappers that (verified in
     the disassembly) were **not actually inlined**, used a different
     midstate-copy method, and let the compiler optimize the surrounding
     loop differently — so −15% measured incidental codegen differences,
     not the indirection. Corrected by isolating the single variable
     (same call site, direct `f(x)` vs. `volatile`-laundered `(*fp)(x)`).
     **Retest confirmed** (3 runs: −0.06%, +0.36%, +0.10% — noise around
     zero; the sign flips between runs, which is itself the signal that
     there is no measurable effect). Disassembly check: the corrected
     bench object has exactly 1 `blr` (the indirect path) and direct
     `bl`s (the direct path) — the comparison is now clean. The full
     methodology lesson is written up as a reusable rule in
     `Req/BENCH.md` §4a. Reproduce: `./build/ub_bench`.
2. **Self-test battery unified (U1).** `ub_core.c`'s `selftest_kernel`
   and `ub_gate_test.c`'s check now share one routine
   (`ub_kernel_matches_oracle` in `ub_selftest.c`) — see that file. No
   longer duplicated.
3. **NEON kernel: correct but slower on this core — MEASURED (U2).**
   A real NEON compress kernel (`src/kernel_neon.c`, from the §1c single
   donor, provenance-pinned) was added, stress-proven correct (3021
   byte-for-byte checks vs. oracle across all block boundaries, large
   multi-block, chunked-update, persona on/off), and benchmarked. On the
   Apple M4 it is **slower than scalar in both shapes**: leaf 0.70x,
   bulk 0.55x MiB/s — the 2-lane-NEON-vs-strong-scalar loss the design
   predicted (`../UniBlake.md` §2b). Per §1c ("beats or ties portable,
   else stays off") NEON is **registered and forceable but NOT
   auto-selected** on this core; auto stays on scalar `ref`. NEON may
   still win on weaker aarch64 cores — the win-list in
   `choose_kernel_from_cpu()` is where a measured win gets promoted.
   `ref_unrolled` is the fastest kernel *here* (~92 ns/leaf) but that is
   one machine/compiler and treated as unverified — its details and the
   OPEN multi-platform-benchmark follow-on are in the kernel file
   (`src/kernel_ref_unrolled.c`); `ref` stays the default until it runs.
   - *Generation-aware selection (added):* the probe now reports full
     CPU identity — vendor, brand string, arch, and generation
     coordinates (x86 family/model/stepping; ARM implementer/part) via
     `ub_detect_cpu_info()` — because "wins on its hardware" means the
     *microarchitecture*, not the ISA (Platforms.md §5: same NEON code
     faster on A53, slower on A57). `choose_kernel_from_cpu()` consults
     it as an explicit **per-microarch win-list** (currently: no SIMD
     entry — Apple M-series measured a loss). `ub_test`/`ub_kbench`
     print the identity so every figure is tied to its exact machine
     (§4a). On this box: `aarch64 Apple "Apple M4 Pro"`.
4. **Oracle-by-#include works cleanly.** Compiling the vendored `.c`
   under a renamed symbol prefix gives an untouched in-tree oracle with
   zero edits to the reference — the pattern generalizes to any future
   vendored reference.
5. **How we run a non-standard internal state AND the vendored default,
   at once.** `struct ub_state` (`src/ub_internal.h`) is *not* the
   reference's `blake2b_state`: it keeps the same six mathematical
   fields (h/t/f/buf/buflen/outlen) but **drops the trailing
   `last_node`** byte. That is deliberate scoping, not drift —
   `last_node` is used only by tree/parallel finalization (`blake2bp`),
   which uniblake does not implement (sequential BLAKE2b only), so the
   field would be dead state. The reason this divergence is *safe* is
   the core architectural decision: **our kernels and the vendored
   reference never share a struct.** Our kernels mutate `ub_state`; the
   oracle (`src/ub_oracle.c`) mutates the reference's own full
   `blake2b_state` inside its own translation unit (symbol-prefixed,
   byte-for-byte untouched). The two only ever have to agree on the
   *mathematical midstate* — the h/t/f/buf values — which the oracle
   gate proves identical, not on any C layout. So the vendored default
   stays authoritative and unmodified (§1a invariant 1) while uniblake
   carries a leaner, purpose-scoped live struct. This is the concrete
   payoff of "opaque state + oracle-by-#include" (`../UniBlake.md` §4,
   anti-pattern-#1 defense): the internal structure is free to differ
   from any reference precisely *because* nothing outside — no caller,
   no oracle — depends on its layout.
6. **Code-inspection record: passing, copies, storage classes, mem\*,
   buffers — clean, no defects.** One consolidated audit of the library:
   - *Passing/copies*: every `ub_state` passes by pointer — no accidental
     by-value struct copies; the only copies are the deliberate midstate
     `ub_blake2b_copy` (a `memcpy`).
   - *Storage classes*: hot-path locals (`m[16]`/`v[16]`, 256 B — the
     intrinsic BLAKE2b working set, same as the reference; can't be
     smaller without changing the algorithm) are automatic, which is
     what lets the compiler keep them in registers (aarch64 has 31 GPRs);
     `register` is a modern no-op and is unused. `volatile` appears
     *only* in the benchmarks as intentional optimizer barriers, never in
     the library. IV/sigma are `const` and extern-linked for
     one-definition cross-TU sharing (correctly not `static`).
     const-correctness on input pointers is right.
   - *mem\* audit* (the "always-suspect" pass over all 18 memcpy/memset):
     all bounds-safe, and now each site carries a `/* bounded: … */`
     comment stating why. The runtime-sized ones also carry a
     compiled-out `assert()` on the invariant the public checks don't
     cover — `update`'s `buflen + inlen ≤ 128`, `final`'s `buflen ≤ 128`
     and `outlen ≤ 64` — so a debug build (`-DNDEBUG` absent) traps a
     broken invariant instead of overflowing `buf`, while release pays
     nothing (verified: both debug and Release/NDEBUG builds pass 6/6).
     *Fixed-size question, checked against the reference*: the three
     runtime-sized copies (`memcpy(buf+buflen, in, inlen)`,
     `memset(buf+buflen, 0, 128-buflen)`, `memcpy(out, buffer, outlen)`)
     are **byte-for-byte the vendored reference's own** (blake2b-ref.c
     lines 242/261/267). They cannot be made fixed-size — `inlen`,
     `buflen`, `outlen` are inherently runtime values, and forcing a
     constant 128-/64-byte copy would over-copy garbage. So we do not
     "harden" past what the canonical reference does; the fixed-size
     copies we *can* have (the 16-byte persona, the struct/snapshot
     copies) already use constant lengths.
   - *Error-handling model, two tiers*: **external-input checks return
     error codes and stay in release** — `init`/`final` bounds
     violations return -1, `import` returns specific `UB_IMPORT_*`
     codes; a caller passing a too-small output buffer or a corrupt
     snapshot gets a clean rejection, never UB. **Internal invariants use
     `assert()` (debug-only)** — these can only fail via a library bug,
     not caller misuse, so they are development guardrails, not release
     control flow. The split is deliberate: never assert on data a caller
     controls, never return an error code for a condition only a bug can
     cause.
   - *Buffer value guarantee*: `buf` bytes at index `≥ buflen` are
     **unspecified during streaming and explicitly zeroed at finalize**
     (`memset(buf+buflen, 0, 128-buflen)` before the last compress) —
     standard BLAKE2b padding. Nothing reads past `buflen` before that
     zeroing, so the undefined tail never affects output; the 0xAA
     output-bounds test proves finalize writes exactly `outlen` bytes.
7. **Import is a full reset — PROVEN (task 19).** `ub_blake2b_import`
   `memset`s the whole `ub_state` to zero then overwrites every field,
   so no field survives from a prior use. Two tests: (a) importing into a
   *dirty mid-operation* state (different persona, wrong outlen, a
   500-byte half-absorbed message polluting t/buf/buflen) yields
   byte-identical output to a fresh import; (b) importing into a
   0x00-filled vs. a **0x55-filled** target produces byte-identical
   structs. *Why 0x55* (0b01010101): a value that differs from the 0x00
   target in every bit, so any byte import left unwritten would surface
   as 0x55 residue in the diff. (It is also the bitwise complement of
   the 0xAA the output-bounds test uses, keeping the two "sentinel"
   values a recognizable pair; neither is sourced from a reference —
   they are just conspicuous non-zero patterns.) `ub_state` has no
   hidden fields beyond the six serialized, so "reset all including
   internal" is total.

## Reproduce

```sh
cd BLAKE/uniblake
cmake -S . -B build -DUB_ENABLE_BROKEN_KERNEL=ON
cmake --build build
ctest --test-dir build --output-on-failure   # 4/4 pass
./build/ub_test                               # human-readable run
./build/ub_gate_test                          # gate-rejection proof
```
