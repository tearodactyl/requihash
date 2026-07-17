/* uniblake internal — the live state and the compress-kernel seam.
 *
 * The live state (ub_state) is a dynamically-updated internal data
 * structure, free to change (§4). Nothing external depends on its
 * layout; the public header keeps it opaque.
 *
 * Relationship to the vendored reference's `blake2b_state`
 * (../../vendor/blake2/blake2.h): ub_state carries the SAME first six
 * fields in the SAME order — h[8], t[2], f[2], buf[128], buflen,
 * outlen — and OMITS the reference's trailing `uint8_t last_node`. This
 * is a deliberate, correct scoping choice, NOT an accidental drift:
 * `last_node` is read only by `blake2b_set_lastnode`, which fires only
 * in the tree/parallel finalization path (blake2bp). uniblake
 * implements SEQUENTIAL BLAKE2b only — no tree mode — so it never sets
 * or reads `last_node`; keeping the field would be dead state.
 *
 * Crucially, the two structs never have to agree, because they are
 * never shared: our kernels operate on `ub_state`, while the ORACLE
 * (ub_oracle.c) operates on the reference's own full `blake2b_state`
 * inside its own translation unit. The two implementations share only
 * the *mathematical* midstate (the h/t/f/buf values), which the oracle
 * gate proves byte-identical — not a C struct layout. That decoupling
 * (opaque state + oracle-by-#include, §3/§4 of UniBlake.md) is exactly
 * what lets uniblake run a leaner internal state while the vendored
 * default stays untouched and authoritative. See STATUS.md finding 5.
 * Provenance: ../PROVENANCE.md.
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

/* The live internal state.
 *
 * REUSE OF THE STANDARD LAYOUT. The first six fields reuse the vendored
 * `blake2b_state`'s exact names, types, and order
 * (../vendor/blake2/blake2.h). That reuse is deliberate and load-
 * bearing, not incidental: it lets the vendored SIMD round headers
 * (kernel_neon.c includes blake2b-round.h unmodified) read `S->h/t/f`
 * by the names they expect, keeps the compress-setup derivation
 * diff-able line-for-line against the reference (correctness rests on
 * matching it, proven by the oracle gate), and makes the snapshot
 * export/import a straight field walk. Do NOT rename or reorder these
 * six. The one standard field intentionally omitted is `last_node`,
 * used only by tree/`blake2bp` finalization, which this sequential-only
 * primitive never runs (it would be dead state). If tree hashing is
 * ever added, `last_node` returns in its standard trailing position.
 *
 * ADDING FIELDS. Any uniblake-specific field we later need (a family
 * tag for a BLAKE3 path, a finalized-guard flag, a cached kernel id)
 * is APPENDED AFTER the standard six, so their offsets never move and
 * the vendored-header drop-in and snapshot walk keep working; the
 * divergence stays a visible addition at the end rather than a
 * reshuffle. We do not replace a standard field with a different
 * representation (that forfeits the diff-ability and header reuse for a
 * micro-optimization the measurements don't justify — the midstate copy
 * is already cheap, U1/U2), and we do not push transient per-compress
 * working state (the expanded message, SIMD lane scratch) into this
 * struct: that belongs in a kernel local on the stack, because the leaf
 * loop copies this state per leaf (2^21 times at n=200) and every added
 * field taxes that copy. A union overlaying alternate views of the same
 * bytes is conceivable only for a strictly mutually-exclusive
 * same-bytes kernel need, with a documented invariant — none exists. */
struct ub_state {
  uint64_t h[8];      /* chaining state */
  uint64_t t[2];      /* message byte counter */
  uint64_t f[2];      /* finalization flags */
  uint8_t  buf[UB_BLOCKBYTES]; /* partial-block buffer (bytes >= buflen unused) */
  size_t   buflen;    /* valid bytes in buf */
  size_t   outlen;    /* requested digest length, 1..64 */
  /* --- uniblake additions append here (see above); none yet --- */
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
#if defined(__aarch64__) || defined(_M_ARM64)
void ub_compress_neon(struct ub_state *S, const uint8_t *blocks, size_t nblocks);
#endif
#if defined(UB_ENABLE_BROKEN_KERNEL)
/* A deliberately-wrong kernel, compiled only under this flag, so a test
 * can prove the self-test gate (§5) actually rejects a bad variant. */
void ub_compress_broken(struct ub_state *S, const uint8_t *blocks, size_t nblocks);
#endif

/* Shared BLAKE2b constants (one definition, used by every scalar
 * kernel). */
extern const uint64_t ub_blake2b_IV[8];
extern const uint8_t  ub_blake2b_sigma[12][16];

/* --- streaming primitives, kernel passed explicitly (defined in
 * ub_core.c). Exposed here so the shared self-test (ub_selftest.c) can
 * drive any kernel through the exact same streaming path the public
 * API uses, rather than re-implementing it. Internal-linkage API. */
int ub_init_with(struct ub_state *S, size_t outlen,
                 const uint8_t *personal, ub_compress_fn fn);
int ub_update_with(struct ub_state *S, const void *in, size_t inlen,
                   ub_compress_fn fn);
int ub_final_with(struct ub_state *S, void *out, size_t outlen,
                  ub_compress_fn fn);

/* The single self-test battery (ub_selftest.c). Drives `fn` through
 * the streaming primitives across a fixed len × outlen × persona
 * battery and compares byte-for-byte to the pristine oracle. Returns 0
 * on full agreement, -1 on any mismatch/error. Used by BOTH the core's
 * gate and the standalone gate-rejection test — one definition, no
 * duplication (§1d oracle type 1). */
int ub_kernel_matches_oracle(ub_compress_fn fn);

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
