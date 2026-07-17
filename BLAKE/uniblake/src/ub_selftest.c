/* uniblake shared self-test battery (§1d oracle type 1).
 *
 * ONE definition of "does this kernel match the pristine oracle,"
 * used by both the core's startup/force gate (ub_core.c) and the
 * standalone gate-rejection test (tests/ub_gate_test.c). Previously
 * duplicated in both; unified here per the U1 finding.
 *
 * Drives the candidate compress `fn` through the real streaming
 * primitives (ub_init_with/update/final) so the test path is the same
 * code the public API runs, then compares byte-for-byte to the
 * untouched vendored reference across a battery spanning empty,
 * sub-block, exact-block, multi-block, unaligned-tail, and
 * personalized inputs.
 */
#include "ub_internal.h"
#include "ub_oracle.h"

#include <string.h>

int ub_kernel_matches_oracle(ub_compress_fn fn) {
  static const size_t lens[] = { 0, 1, 55, 64, 127, 128, 129, 256, 1000 };
  uint8_t in[1000];
  for (size_t i = 0; i < sizeof in; ++i) in[i] = (uint8_t)(i * 131 + 7);
  const uint8_t persona[16] = { 'u','n','i','b','l','a','k','e',
                                '-','s','e','l','f','t','s','t' };

  for (size_t p = 0; p < 2; ++p) {
    const uint8_t *pers = p ? persona : 0;
    for (size_t li = 0; li < sizeof(lens) / sizeof(lens[0]); ++li) {
      size_t n = lens[li];
      uint8_t got[64], want[64];

      struct ub_state S;
      if (ub_init_with(&S, 64, pers, fn) != 0) return -1;
      if (ub_update_with(&S, in, n, fn) != 0) return -1;
      if (ub_final_with(&S, got, 64, fn) != 0) return -1;

      ub_oracle_blake2b(want, 64, in, n, pers);
      if (memcmp(got, want, 64) != 0) return -1;
    }
  }
  return 0;
}
