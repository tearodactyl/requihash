/* uniblake compress kernel: `neon` — AArch64 NEON (U2, first real SIMD).
 *
 * Rewritten-from the BLAKE2/BLAKE2 package's NEON reference
 * (blake2b-neon.c), derived to our multi-block seam and our state.
 * The round + message-load machinery is the donor's, VENDORED
 * UNMODIFIED at ../vendor/neon/ (blake2b-round.h, blake2b-load-neon.h;
 * pinned commit ed1974e 2023-02-12) — this is the §1c single NEON
 * donor. The compress body's state setup mirrors the donor's
 * blake2b_compress exactly; only the surrounding kernel signature
 * (multi-block loop over our struct ub_state) is ours. Correctness is
 * not assumed from the derivation — the oracle gate proves it
 * byte-for-byte (§1a invariant 1). Provenance: ../PROVENANCE.md.
 *
 * Compiled only on aarch64 (guarded); registered by the core only when
 * present. NEON is base-ISA on aarch64, so no runtime probe is needed
 * to USE it — the probe still reports it for the record.
 */
#include "ub_internal.h"

#if defined(__aarch64__) || defined(_M_ARM64)

#include <arm_neon.h>

/* The donor round macros need `blake2b_IV` by that name and the vext
 * temporaries t0/t1 in scope. We alias our IV and include the vendored
 * headers unmodified. */
static const uint64_t blake2b_IV[8] = {
  0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL,
  0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
  0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
  0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL
};

#include "../vendor/neon/blake2b-round.h"

static void compress_one(struct ub_state *S, const uint8_t block[UB_BLOCKBYTES]) {
  const uint64x2_t m0 = vreinterpretq_u64_u8(vld1q_u8(&block[  0]));
  const uint64x2_t m1 = vreinterpretq_u64_u8(vld1q_u8(&block[ 16]));
  const uint64x2_t m2 = vreinterpretq_u64_u8(vld1q_u8(&block[ 32]));
  const uint64x2_t m3 = vreinterpretq_u64_u8(vld1q_u8(&block[ 48]));
  const uint64x2_t m4 = vreinterpretq_u64_u8(vld1q_u8(&block[ 64]));
  const uint64x2_t m5 = vreinterpretq_u64_u8(vld1q_u8(&block[ 80]));
  const uint64x2_t m6 = vreinterpretq_u64_u8(vld1q_u8(&block[ 96]));
  const uint64x2_t m7 = vreinterpretq_u64_u8(vld1q_u8(&block[112]));

  uint64x2_t row1l, row1h, row2l, row2h;
  uint64x2_t row3l, row3h, row4l, row4h;
  uint64x2_t b0, b1, t0, t1;

  const uint64x2_t h0 = row1l = vld1q_u64(&S->h[0]);
  const uint64x2_t h1 = row1h = vld1q_u64(&S->h[2]);
  const uint64x2_t h2 = row2l = vld1q_u64(&S->h[4]);
  const uint64x2_t h3 = row2h = vld1q_u64(&S->h[6]);

  row3l = vld1q_u64(&blake2b_IV[0]);
  row3h = vld1q_u64(&blake2b_IV[2]);
  row4l = veorq_u64(vld1q_u64(&blake2b_IV[4]), vld1q_u64(&S->t[0]));
  row4h = veorq_u64(vld1q_u64(&blake2b_IV[6]), vld1q_u64(&S->f[0]));

  ROUND(0);  ROUND(1);  ROUND(2);  ROUND(3);
  ROUND(4);  ROUND(5);  ROUND(6);  ROUND(7);
  ROUND(8);  ROUND(9);  ROUND(10); ROUND(11);

  vst1q_u64(&S->h[0], veorq_u64(h0, veorq_u64(row1l, row3l)));
  vst1q_u64(&S->h[2], veorq_u64(h1, veorq_u64(row1h, row3h)));
  vst1q_u64(&S->h[4], veorq_u64(h2, veorq_u64(row2l, row4l)));
  vst1q_u64(&S->h[6], veorq_u64(h3, veorq_u64(row2h, row4h)));
}

void ub_compress_neon(struct ub_state *S, const uint8_t *blocks, size_t nblocks) {
  for (size_t b = 0; b < nblocks; ++b)
    compress_one(S, blocks + b * UB_BLOCKBYTES);
}

#endif /* aarch64 */
