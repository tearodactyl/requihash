/* uniblake gate-rejection test (§5 made observable).
 *
 * Proves the self-test gate REJECTS a kernel that disagrees with the
 * oracle — the negative case that makes "forcing selects, never
 * bypasses the gate" a demonstrated property, not an assertion. Built
 * only when UB_ENABLE_BROKEN_KERNEL is on.
 *
 * It calls the SAME routine the core's gate uses
 * (ub_kernel_matches_oracle, ub_selftest.c) — no duplicated criterion
 * (U1 finding 2). ub_kernel_matches_oracle returns 0 on full oracle
 * agreement, -1 on any mismatch.
 */
#include "uniblake.h"
#include "ub_internal.h"

#include <stdio.h>

int main(void) {
  printf("=== uniblake gate-rejection test ===\n");
  int fails = 0;

  /* ref must match (control): the shared gate routine returns 0. */
  int ref_rc = ub_kernel_matches_oracle(ub_compress_ref);
  printf("  %s ref kernel passes the gate (control)\n",
         ref_rc == 0 ? "ok  " : "FAIL");
  if (ref_rc != 0) fails++;

#if defined(UB_ENABLE_BROKEN_KERNEL)
  /* broken must be rejected: the shared gate routine returns nonzero. */
  int broken_rc = ub_kernel_matches_oracle(ub_compress_broken);
  printf("  %s broken kernel is rejected by the gate (nonzero)\n",
         broken_rc != 0 ? "ok  " : "FAIL");
  if (broken_rc == 0) fails++;
#else
  printf("  skip broken-kernel case (rebuild with -DUB_ENABLE_BROKEN_KERNEL=ON)\n");
#endif

  printf("=== %s ===\n", fails ? "FAILED" : "GATE PROVEN");
  return fails ? 1 : 0;
}
