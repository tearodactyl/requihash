/* uniblake internal — the live state and the compress-kernel seam.
 *
 * The live state (ub_state) is a dynamically-updated internal data
 * structure, free to change (§4). Nothing external depends on its
 * layout; the public header keeps it opaque. Fields mirror the BLAKE2b
 * mathematical state (chaining h, counter t, finalization f, byte
 * buffer) — this is derived from the vendored reference
 * (../../vendor/blake2/blake2b-ref.c, Neves, CC0) and stays
 * byte-compatible with it by construction (validated by the oracle
 * gate). Provenance: ../PROVENANCE.md.
 *
 * The pluggable unit is the compress function (§2, "compress-first,
 * not compress-only"). A kernel is a `ub_compress_fn`; the streaming
 * core calls it through a selected pointer. Broader granularity (batch
 * kernels) is a later phase and is why the seam is a registered
 * function, not a hardcoded call.
 */
#ifndef UB_INTERNAL_H
#define UB_INTERNAL_H

#include <stdint.h>
#include <stddef.h>
#include "uniblake.h"

#define UB_BLOCKBYTES 128

struct ub_state {
  uint64_t h[8];
  uint64_t t[2];
  uint64_t f[2];
  uint8_t  buf[UB_BLOCKBYTES];
  size_t   buflen;
  size_t   outlen;
};

/* A compress kernel: absorb `nblocks` 128-byte blocks into S->h,
 * advancing nothing else (counter/finalization handled by the core).
 * Every kernel — ref, unrolled, future SIMD — has this signature; this
 * is the shape a runtime-dispatched table selects among, and (§2's
 * flagged concern) the shape whose exclusive indirect-call cost must be
 * reviewed before U1 hardens. */
typedef void (*ub_compress_fn)(struct ub_state *S,
                               const uint8_t *blocks, size_t nblocks);

/* The registered kernels (defined in their own translation units). */
void ub_compress_ref(struct ub_state *S, const uint8_t *blocks, size_t nblocks);
void ub_compress_ref_unrolled(struct ub_state *S, const uint8_t *blocks, size_t nblocks);
#if defined(UB_ENABLE_BROKEN_KERNEL)
/* A deliberately-wrong kernel, compiled only under this flag, so a test
 * can prove the self-test gate (§5) actually rejects a bad variant. */
void ub_compress_broken(struct ub_state *S, const uint8_t *blocks, size_t nblocks);
#endif

/* Shared BLAKE2b constants (one definition, used by every scalar
 * kernel). */
extern const uint64_t ub_blake2b_IV[8];
extern const uint8_t  ub_blake2b_sigma[12][16];

/* little-endian load/store, kernel-shared. */
static inline uint64_t ub_load64(const uint8_t *p) {
  return (uint64_t)p[0]        | ((uint64_t)p[1] << 8)  |
         ((uint64_t)p[2] << 16) | ((uint64_t)p[3] << 24) |
         ((uint64_t)p[4] << 32) | ((uint64_t)p[5] << 40) |
         ((uint64_t)p[6] << 48) | ((uint64_t)p[7] << 56);
}
static inline void ub_store64(uint8_t *p, uint64_t v) {
  p[0] = (uint8_t)v;        p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
  p[4] = (uint8_t)(v >> 32); p[5] = (uint8_t)(v >> 40);
  p[6] = (uint8_t)(v >> 48); p[7] = (uint8_t)(v >> 56);
}
static inline uint64_t ub_rotr64(uint64_t w, unsigned c) {
  return (w >> c) | (w << (64 - c));
}

#endif /* UB_INTERNAL_H */
