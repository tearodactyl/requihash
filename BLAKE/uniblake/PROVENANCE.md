# uniblake — provenance manifest (PoC)

Per `../UniBlake.md` §1a invariant 2: every outside input recorded in
its mode — **plugged in** (vendored-unmodified, commit pinned),
**rewritten from** (derived; source + deviation list), or **used as a
direct example** (original here, informed-by citation). The PoC has one
external input; everything else is original code authored in this repo.

## Inputs

### `../vendor/blake2/blake2b-ref.c` (+ `blake2.h`, `blake2-impl.h`)

- **Mode: plugged in** (vendored-unmodified) **as the oracle backend**,
  and **rewritten from** as the `ref`/`ref_unrolled` compress kernels.
- **Upstream**: BLAKE2/BLAKE2 package, Samuel Neves et al., CC0 /
  OpenSSL / Apache-2.0 (tri-license). Pinned commit `ed1974e`
  (2023-02-12), per `../vendor/blake2/PROVENANCE.md` and `../BLAKE.md`
  §0. This is the repository's single canonical vendored BLAKE2b.
- **How used, two ways**:
  1. **Oracle (plugged in, untouched)**: `src/ub_oracle.c` `#include`s
     `blake2b-ref.c` verbatim, renaming its public symbols to a
     `ubref_` prefix via `#define` *before* the include. No line of the
     reference is edited — symbol renaming is a build/platform-class
     action (`../UniBlake.md` §1d), byte-invariant. This satisfies §1a
     invariant 1 (an untouched reference remains linked as the oracle).
  2. **Kernels (rewritten from)**: `src/kernel_ref.c` and
     `src/kernel_ref_unrolled.c` derive the compress step from the
     reference's `G`/`ROUND` structure.
     - **Deviations from the reference** (the §1a "rewritten from"
       deviation list):
       - Multi-block kernel signature `(state, blocks, nblocks)` instead
         of the reference's single-block `blake2b_compress(state,
         block)` — the §2 pluggable-unit seam. Build/platform-class.
       - Uses the repo-local `ub_load64`/`ub_rotr64` inline helpers
         (`src/ub_internal.h`) instead of the reference's
         `load64`/`rotr64` — identical arithmetic, avoids pulling
         `blake2-impl.h` into the kernels. Build/platform-class.
       - `ref_unrolled` re-expresses the round as an inline `g()`
         function over a flat sigma schedule instead of nested macros —
         an *expression* change, algorithmically identical.
     - **No algorithmic change** (`../UniBlake.md` §1d): the twelve
       rounds, the sigma schedule (`src/ub_const.c`, copied
       value-for-value from the reference), the G mixing, and the
       finalization xor are byte-identical. **Proven** by the oracle
       gate: every kernel is validated byte-for-byte against the
       untouched oracle across a len × outlen × persona battery, and
       `ref`/`ref_unrolled` both pass (`tests/ub_test.c`,
       `tests/ub_gate_test.c`).

## Original code authored in this repo (no external provenance)

- `include/uniblake.h` — the `ub_` public API.
- `src/ub_core.c` — streaming core, kernel registry, dispatch,
  `UB_FORCE_IMPL`, self-test gate. The streaming *logic* (update
  buffering, counter, padding, finalize) follows the reference's
  algorithm (rewritten-from, byte-identical, oracle-proven); the
  registry/dispatch/gate machinery is original.
- `src/ub_probe.c`, `src/ub_probe.h` — CPU detection. Mechanisms
  (`cpuid`, `getauxval`, `sysctlbyname`) are standard OS/ISA
  interfaces, not derived from any BLAKE library.
- `src/kernel_broken.c` — original test asset (`ref` with one round
  dropped) to prove the gate rejects a bad kernel.
- `CMakeLists.txt`, `BUILD.md`, `PLAN.md`, this file — original.

### `vendor/neon/blake2b-round.h`, `vendor/neon/blake2b-load-neon.h`

- **Mode: plugged in** (vendored-unmodified) — the NEON round macros
  (G1/G2, DIAGONALIZE, `vext`-based rotations) and message-permutation
  load macros, used by the `neon` compress kernel (U2).
- **Upstream**: BLAKE2/BLAKE2 package `neon/` subdirectory, Samuel
  Neves et al., CC0/OpenSSL/Apache-2.0. Pinned commit `ed1974e`
  (2023-02-12) — the same pin as the scalar reference (`../BLAKE.md`
  §0). This is the §1c **single NEON donor**.
- **How used**: `src/kernel_neon.c` `#include`s both headers unmodified
  and calls the donor's `ROUND(r)` macro; the compress body's
  state-setup (loading `S->h/t/f` into NEON rows, the final xor-store)
  mirrors the donor's `blake2b_compress` exactly. Only the surrounding
  multi-block kernel signature is ours (**rewritten from**, deviation:
  the `(state, blocks, nblocks)` seam vs. the donor's single-block
  `blake2b_compress`). No algorithmic change — the rounds, rotations,
  and message schedule are the donor's. **Proven** byte-for-byte
  against the scalar oracle by the stress test (3021 checks) and the
  self-test gate.

## Not yet present (future phases, will extend this manifest)

- x86 SIMD kernels (U2 continued): donors per `../UniBlake.md` §1c/§2b
  — libsodium AVX2 compress TUs (x86 single-donor). Pinned here when
  adopted (needs real x86 to validate/measure — A7).
- BLAKE3 (U6): official BLAKE3 C, vendored, pinned when adopted.
