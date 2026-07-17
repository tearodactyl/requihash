# uniblake — PoC status & findings

**Result: PoC GREEN on arm64 macOS (M4).** The UniBlake shape is
proven runnable, not just designed. Built with Apple clang 21 / CMake
4.2; all four ctests pass.

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

## What the PoC deliberately does NOT do

- **No SIMD kernels** — the probe result doesn't yet change kernel
  choice (auto → `ref`). SIMD is U2, gated on measurement (A13/A7).
- **No x86/Windows run** — those code paths are written and
  guard-balanced but unrun this session (no hardware). `BUILD.md`
  grades them STRUCTURED, NOT YET RUN, with verbatim directions.
- **No state snapshot** — `ub_blake2b_copy` copies the *live* struct
  (midstate). The versioned export/import format (§4) is U3.
- **No batch/"many"** — R6, deferred (§6a materials only).
- **No Rust wrapper / BLAKE3** — U5 / U6.

## Findings worth carrying forward

1. **The dispatch indirection is real and measurable-later.** Every
   compress goes through `g_active_fn`. `../UniBlake.md` §2 already
   flags that an *exclusive* table may block inlining/LTO; the PoC
   confirms the shape but does not yet measure the cost. This is the
   first thing U1-proper should quantify (a `ref`-inlined build vs. the
   dispatched build on the leaf shape).
2. **The self-test battery is duplicated** between `ub_core.c`'s
   `selftest_kernel` and `ub_gate_test.c`'s `matches_oracle`. They
   should share one routine before U1 hardens (noted in the gate test).
3. **`ref_unrolled` is not faster** — it exists only to exercise
   dispatch. No performance claim; the unroll is a plumbing witness.
   A real second kernel (NEON) replaces its role at U2.
4. **Oracle-by-#include works cleanly.** Compiling the vendored `.c`
   under a renamed symbol prefix gives an untouched in-tree oracle with
   zero edits to the reference — the pattern generalizes to any future
   vendored reference.

## Reproduce

```sh
cd BLAKE/uniblake
cmake -S . -B build -DUB_ENABLE_BROKEN_KERNEL=ON
cmake --build build
ctest --test-dir build --output-on-failure   # 4/4 pass
./build/ub_test                               # human-readable run
./build/ub_gate_test                          # gate-rejection proof
```
