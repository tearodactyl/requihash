# BLAKE2 provenance — vendored bytes + uniblake manifest

This file is the single provenance record for the BLAKE2 code in this
repository. It has two parts:

- **Part 1 — the vendored BLAKE2 pin** (§"Vendored BLAKE2 reference"):
  the authoritative source/commit/license for the vendored reference
  bytes. This is a **repo-wide** record — the solver-corpus ports (`rz`,
  `cs`, `rk/original`) and `blake2-rs` all build against these bytes and
  cite this section.
- **Part 2 — the uniblake manifest** (§"uniblake inputs" onward): how
  *uniblake's own* source files derive from those bytes, classified by
  the four provenance modes (**vendored / adjusted / rewritten-from /
  authored**) defined in `../UniBlake.md` §1a invariant 2 — not restated
  here.

## Vendored BLAKE2 reference (the repo-wide pin)

Files `blake2.h`, `blake2-impl.h`, `blake2b-ref.c` in
`../vendor/blake2/` are copied **unmodified** from the `ref/` directory
of github.com/BLAKE2/BLAKE2 at commit `ed1974e` (2023-02-12, upstream
tip as of vendoring on 2026-07-16; local clone
`~/Work/ZK/ZKs/BLAKE/blake2-reference`). Author: Samuel Neves; license:
CC0 1.0 / OpenSSL License / Apache-2.0, at your option (header of each
file). The NEON headers in `../vendor/blake2`'s sibling `neon/` donor
(used only by uniblake, see Part 2) share the same package, author,
commit, and license, from the package's `neon/` subdirectory.

This is the canonical BLAKE2b implementation for every C/C++ consumer in
this repository (decision record: `../BLAKE.md`). Build-time consumers
reference the `vendor/blake2/` directory **repo-relative** — never by
absolute path. To update: re-copy from the clone, record the new commit
here, and re-run every consumer's tests (`rz`, `cs`, `rk/original`,
uniblake).

## uniblake inputs (Part 2 — the per-file manifest)

### `../vendor/blake2/blake2b-ref.c` (+ `blake2.h`, `blake2-impl.h`)

- **Upstream pin**: the vendored-bytes section above.
- **Modes**: **adjusted** as the oracle backend (vendored bytes,
  renamed at the edge — see below), and **rewritten-from** as the
  `ref`/`ref_unrolled` compress kernels.
- **How used, two ways**:
  1. **Oracle (adjusted)**: `src/ub_oracle.c` `#include`s
     `blake2b-ref.c` verbatim, renaming its public symbols to a
     `ubref_` prefix via `#define` *before* the include. No algorithmic
     line is edited; the only change is the symbol-namespace rename at
     the edge — the textbook **adjusted** case (build/platform-class per
     §1d, byte-invariant output). This still satisfies §1a invariant 1
     (the reference's computed bytes are untouched, so it is a valid
     oracle).
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

## Authored in this repo (no upstream bytes)

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

- **Mode: vendored** (unmodified) — the NEON round macros
  (G1/G2, DIAGONALIZE, `vext`-based rotations) and message-permutation
  load macros, used by the `neon` compress kernel (U2).
- **Upstream pin**: the `neon/`-subdirectory case noted in the
  vendored-bytes section above (same package/author/commit/license as
  the scalar ref). The §1c **single NEON donor**; used only by uniblake.
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
