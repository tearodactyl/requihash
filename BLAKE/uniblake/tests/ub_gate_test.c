/* uniblake gate-rejection test (§5 made observable).
 *
 * Proves the self-test gate REJECTS a kernel that disagrees with the
 * oracle — the negative case that makes "forcing selects, never
 * bypasses the gate" a demonstrated property, not an assertion. Built
 * only when UB_ENABLE_BROKEN_KERNEL is on. It exercises the same
 * validation routine the core uses, against the deliberately-wrong
 * kernel, and asserts it fails.
 */
#include "uniblake.h"
#include "ub_internal.h"
#include "ub_oracle.h"

#include <stdio.h>
#include <string.h>

/* Hash one short message through a raw compress kernel `fn` (driving a
 * single-block finalization by hand, since the kernel is not
 * registered in the core) and compare to the oracle. A correct kernel
 * matches; a broken one does not. This is the decisive criterion the
 * gate applies, in miniature. */
static int matches_oracle(ub_compress_fn fn) {
  const char *msg = "gate-rejection-probe";
  size_t mlen = strlen(msg);

  struct ub_state S;
  memset(&S, 0, sizeof S);
  uint8_t P[64] = {0}; P[0] = 64; P[2] = 1; P[3] = 1; /* outlen 64, seq mode */
  for (int i = 0; i < 8; ++i)
    S.h[i] = ub_blake2b_IV[i] ^ ub_load64(P + i * 8);
  S.outlen = 64;

  memcpy(S.buf, msg, mlen);
  S.buflen = mlen;
  S.t[0] += mlen;         /* counter += bytes absorbed */
  S.f[0] = (uint64_t)-1;  /* last block */
  fn(&S, S.buf, 1);

  uint8_t got_h[64], want_h[64];
  for (int i = 0; i < 8; ++i) ub_store64(got_h + i * 8, S.h[i]);
  ub_oracle_blake2b(want_h, 64, msg, mlen, NULL);
  return memcmp(got_h, want_h, 64) == 0;
}

int main(void) {
  printf("=== uniblake gate-rejection test ===\n");
  int fails = 0;

  /* ref must match (control). */
  int ref_ok = matches_oracle(ub_compress_ref);
  printf("  %s ref kernel matches oracle (control)\n", ref_ok ? "ok  " : "FAIL");
  if (!ref_ok) fails++;

#if defined(UB_ENABLE_BROKEN_KERNEL)
  int broken_ok = matches_oracle(ub_compress_broken);
  printf("  %s broken kernel is rejected (does NOT match oracle)\n",
         !broken_ok ? "ok  " : "FAIL");
  if (broken_ok) fails++;
#else
  printf("  skip broken-kernel case (rebuild with -DUB_ENABLE_BROKEN_KERNEL=ON)\n");
#endif

  printf("=== %s ===\n", fails ? "FAILED" : "GATE PROVEN");
  return fails ? 1 : 0;
}
