/* uniblake — unified C/C++ BLAKE2 (PoC).
 *
 * Public API (the `ub_` family). PoC scope: BLAKE2b streaming with the
 * personalization parameter (R2 carries R9), runtime kernel dispatch
 * with registration + forced-impl override (R3 carries R7), oracle
 * self-test gate. See ../PLAN.md for selections, ../../UniBlake.md for
 * the design.
 *
 * Argument order is modern throughout (out first, then lengths) — the
 * legacy 2016 order (BLAKE.md §3.3) is never exposed (anti-pattern #9).
 */
#ifndef UNIBLAKE_H
#define UNIBLAKE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- constants (BLAKE2b) --- */
enum {
  UB_BLAKE2B_BLOCKBYTES = 128,
  UB_BLAKE2B_OUTBYTES   = 64,
  UB_BLAKE2B_PERSONALBYTES = 16
};

/* --- opaque state (anti-pattern #1 defense: no mirrored layout) ---
 *
 * The caller never sees the field layout. Size is reported at runtime
 * by ub_state_size() so a consumer allocating N states does not bake
 * in a compile-time sizeof that could desynchronize from the library.
 *
 * STORAGE IS THE CALLER'S CHOICE — and the library is indifferent to
 * it. A ub_state (and therefore its internal `buf`) may live on the
 * stack (`ub_state s;`), on the heap (`malloc(ub_state_size())`), or in
 * static storage; the API only ever takes `ub_state *`, never copies it
 * by value, and holds no pointers into it across calls. So "is buf
 * static/heap/stack?" is not a correctness question here — there is no
 * hidden aliasing, no internal allocation, and no lifetime the caller
 * doesn't control. The only requirements are alignment (ub_state_align)
 * and that the storage outlives the hashing sequence. The one caveat
 * that IS the caller's: a ub_state holds no lock, so a single instance
 * must not be updated from two threads at once (per-thread copies via
 * ub_blake2b_copy are the intended concurrency model).
 */
typedef struct ub_state ub_state;

/* Bytes to allocate for a ub_state. Runtime-reported, never assumed. */
size_t ub_state_size(void);
/* Alignment required for a ub_state buffer. */
size_t ub_state_align(void);

/* --- streaming API --- */

/* Initialize for an `outlen`-byte digest, no personalization. */
int ub_blake2b_init(ub_state *S, size_t outlen);

/* Initialize with a 16-byte personalization block (R9). `personal`
 * may be NULL to mean all-zero. This is the Equihash-relevant path. */
int ub_blake2b_init_personal(ub_state *S, size_t outlen,
                             const uint8_t personal[UB_BLAKE2B_PERSONALBYTES]);

int ub_blake2b_update(ub_state *S, const void *in, size_t inlen);
int ub_blake2b_final(ub_state *S, void *out, size_t outlen);

/* Copy live state (the midstate pattern: hash a prefix once, copy,
 * append per-leaf). dst must be a valid ub_state buffer. This copies
 * the *live* structure, not the versioned snapshot (§4) — snapshot
 * export/import is a later phase (U3), deliberately not in the PoC. */
int ub_blake2b_copy(ub_state *dst, const ub_state *src);

/* One-shot convenience. */
int ub_blake2b(void *out, size_t outlen, const void *in, size_t inlen,
               const uint8_t personal[UB_BLAKE2B_PERSONALBYTES]);

/* --- state snapshot: versioned export / import (§4) ---
 *
 * Distinct from ub_blake2b_copy (which copies the LIVE struct): a
 * snapshot is a versioned, format-stable WIRE image of the midstate,
 * portable across kernels and (within version rules) across releases.
 * Export a prefix-absorbed state once; import it repeatedly to seed
 * many finalizations, including across processes or persistence.
 */

/* Bytes needed for a snapshot (current format). Runtime-reported. */
size_t ub_snapshot_size(void);

/* Serialize S into `out` (>= ub_snapshot_size() bytes). Returns 0, or
 * -1 if out is NULL / too small. */
int ub_blake2b_export(const ub_state *S, uint8_t *out, size_t outcap);

/* Import result codes: distinguish "not a snapshot" from "a snapshot I
 * can't read", so a caller never silently misreads (§4). */
typedef enum {
  UB_IMPORT_OK = 0,
  UB_IMPORT_EBADARG,    /* NULL args */
  UB_IMPORT_ETRUNCATED, /* input shorter than the declared format */
  UB_IMPORT_EMAGIC,     /* not a uniblake snapshot */
  UB_IMPORT_EFAMILY,    /* wrong hash family (e.g. a BLAKE3 snapshot) */
  UB_IMPORT_EVERSION,   /* uniblake snapshot, unsupported format version */
  UB_IMPORT_ECORRUPT    /* header ok but body fields out of range */
} ub_import_rc;

/* Reconstruct a live state from a snapshot. Returns UB_IMPORT_OK, or a
 * specific nonzero code — a version/family mismatch is a loud error,
 * never a silent misread. */
int ub_blake2b_import(ub_state *S, const uint8_t *in, size_t inlen);

/* --- dispatch / registration / forced-impl (R3 ⊇ R7) --- */

/* Kernel identifiers. Extend as SIMD kernels land (U2). */
typedef enum {
  UB_KERNEL_AUTO = 0,   /* CPU probe selects (default) */
  UB_KERNEL_REF,        /* portable scalar reference */
  UB_KERNEL_REF_UNROLLED, /* experimental unrolled scalar */
  UB_KERNEL_NEON,       /* AArch64 NEON (present only on aarch64 builds) */
  UB_KERNEL__COUNT
} ub_kernel_id;

/* Which kernel the library actually selected for this process. */
ub_kernel_id ub_active_kernel(void);
const char  *ub_kernel_name(ub_kernel_id id);

/* Explicitly select a kernel for this process, bypassing the probe.
 * Equivalent to the UB_FORCE_IMPL env var. Returns 0 on success, -1 if
 * the named kernel is not registered/available. A forced kernel STILL
 * passes the oracle self-test or this returns -1 (§5): forcing selects,
 * it never bypasses the gate. */
int ub_force_kernel(ub_kernel_id id);

/* Run the startup self-test gate now: every registered kernel is
 * validated byte-for-byte against the pristine oracle backend across a
 * fixed vector battery. Returns 0 if all pass, else the (negative)
 * kernel id that failed. Called automatically on first use; exposed
 * for tests. */
int ub_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* UNIBLAKE_H */
