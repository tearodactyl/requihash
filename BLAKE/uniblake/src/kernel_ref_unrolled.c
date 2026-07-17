/* uniblake compress kernel: `ref_unrolled` — experimental scalar.
 *
 * Same algorithm as `ref`, with the G mixing written as an inline
 * function and the round loop expressed over a flat schedule rather
 * than nested macros. Purpose in the PoC: a genuine SECOND registered
 * kernel so variant selection (§2), UB_FORCE_IMPL, and the self-test
 * gate (§5) are exercised against something that is not the oracle
 * itself. It must produce byte-identical output to `ref` — an
 * expression change, not an algorithmic one (§1d) — which the oracle
 * gate proves. Not a performance claim; a dispatch-plumbing witness.
 */
#include "ub_internal.h"

static inline void g(uint64_t v[16], unsigned a, unsigned b, unsigned c,
                     unsigned d, uint64_t x, uint64_t y) {
  v[a] = v[a] + v[b] + x;
  v[d] = ub_rotr64(v[d] ^ v[a], 32);
  v[c] = v[c] + v[d];
  v[b] = ub_rotr64(v[b] ^ v[c], 24);
  v[a] = v[a] + v[b] + y;
  v[d] = ub_rotr64(v[d] ^ v[a], 16);
  v[c] = v[c] + v[d];
  v[b] = ub_rotr64(v[b] ^ v[c], 63);
}

static void round_fn(uint64_t v[16], const uint64_t m[16], const uint8_t *s) {
  g(v, 0, 4, 8,  12, m[s[0]],  m[s[1]]);
  g(v, 1, 5, 9,  13, m[s[2]],  m[s[3]]);
  g(v, 2, 6, 10, 14, m[s[4]],  m[s[5]]);
  g(v, 3, 7, 11, 15, m[s[6]],  m[s[7]]);
  g(v, 0, 5, 10, 15, m[s[8]],  m[s[9]]);
  g(v, 1, 6, 11, 12, m[s[10]], m[s[11]]);
  g(v, 2, 7, 8,  13, m[s[12]], m[s[13]]);
  g(v, 3, 4, 9,  14, m[s[14]], m[s[15]]);
}

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

  for (i = 0; i < 12; ++i) round_fn(v, m, ub_blake2b_sigma[i]);

  for (i = 0; i < 8; ++i) S->h[i] ^= v[i] ^ v[i + 8];
}

void ub_compress_ref_unrolled(struct ub_state *S, const uint8_t *blocks, size_t nblocks) {
  for (size_t b = 0; b < nblocks; ++b)
    compress_one(S, blocks + b * UB_BLOCKBYTES);
}
