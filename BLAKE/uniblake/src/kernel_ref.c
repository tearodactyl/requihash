/* uniblake compress kernel: `ref` — portable scalar.
 *
 * The compress step, derived faithfully from the vendored reference
 * (../../vendor/blake2/blake2b-ref.c, Neves, CC0). The G/ROUND macro
 * structure is the reference's, adapted only to (a) the shared
 * ub_load64/ub_rotr64 helpers and (b) the multi-block kernel signature
 * (§2 seam). This is a build/platform-class adaptation (§1d): the
 * algorithmic steps — the twelve rounds, the sigma schedule, the
 * finalization xor — are byte-identical to the reference, which the
 * oracle gate proves. Provenance: ../PROVENANCE.md.
 */
#include "ub_internal.h"

#define G(r, i, a, b, c, d)                          \
  do {                                               \
    a = a + b + m[ub_blake2b_sigma[r][2 * i + 0]];   \
    d = ub_rotr64(d ^ a, 32);                        \
    c = c + d;                                        \
    b = ub_rotr64(b ^ c, 24);                        \
    a = a + b + m[ub_blake2b_sigma[r][2 * i + 1]];   \
    d = ub_rotr64(d ^ a, 16);                        \
    c = c + d;                                        \
    b = ub_rotr64(b ^ c, 63);                        \
  } while (0)

#define ROUND(r)                       \
  do {                                 \
    G(r, 0, v[0], v[4], v[8],  v[12]); \
    G(r, 1, v[1], v[5], v[9],  v[13]); \
    G(r, 2, v[2], v[6], v[10], v[14]); \
    G(r, 3, v[3], v[7], v[11], v[15]); \
    G(r, 4, v[0], v[5], v[10], v[15]); \
    G(r, 5, v[1], v[6], v[11], v[12]); \
    G(r, 6, v[2], v[7], v[8],  v[13]); \
    G(r, 7, v[3], v[4], v[9],  v[14]); \
  } while (0)

static void compress_one(struct ub_state *S, const uint8_t block[UB_BLOCKBYTES]) {
  uint64_t m[16], v[16];
  size_t i;

  for (i = 0; i < 16; ++i) m[i] = ub_load64(block + i * 8);
  for (i = 0; i < 8; ++i)  v[i] = S->h[i];

  v[8]  = ub_blake2b_IV[0];
  v[9]  = ub_blake2b_IV[1];
  v[10] = ub_blake2b_IV[2];
  v[11] = ub_blake2b_IV[3];
  v[12] = ub_blake2b_IV[4] ^ S->t[0];
  v[13] = ub_blake2b_IV[5] ^ S->t[1];
  v[14] = ub_blake2b_IV[6] ^ S->f[0];
  v[15] = ub_blake2b_IV[7] ^ S->f[1];

  ROUND(0);  ROUND(1);  ROUND(2);  ROUND(3);
  ROUND(4);  ROUND(5);  ROUND(6);  ROUND(7);
  ROUND(8);  ROUND(9);  ROUND(10); ROUND(11);

  for (i = 0; i < 8; ++i) S->h[i] ^= v[i] ^ v[i + 8];
}

void ub_compress_ref(struct ub_state *S, const uint8_t *blocks, size_t nblocks) {
  for (size_t b = 0; b < nblocks; ++b)
    compress_one(S, blocks + b * UB_BLOCKBYTES);
}

#undef G
#undef ROUND
