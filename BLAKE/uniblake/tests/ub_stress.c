/* uniblake stress test (U2) — hammer every kernel vs. the oracle.
 *
 * Beyond the smoke test's fixed battery: many randomized inputs, all
 * block-boundary lengths, long multi-block messages, every outlen, and
 * personalization on/off. Each kernel is forced in turn and every
 * digest is compared byte-for-byte to the untouched oracle. A kernel
 * that diverges anywhere fails the run. Deterministic (seeded LCG, no
 * rand()) so a failure reproduces.
 *
 * This is where a subtly-wrong SIMD kernel (a bad rotate, a lane swap,
 * a boundary off-by-one) gets caught — the smoke test's short inputs
 * might not exercise the failing path; the stress test does.
 */
#include "uniblake.h"
#include "ub_internal.h"
#include "ub_oracle.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

static uint64_t s_lcg = 0x243F6A8885A308D3ULL;
static uint8_t rnd_byte(void) {
  s_lcg = s_lcg * 6364136223846793005ULL + 1442695040888963407ULL;
  return (uint8_t)(s_lcg >> 56);
}

int main(void) {
  printf("=== uniblake stress test ===\n");

  const ub_kernel_id kernels[] = {
    UB_KERNEL_REF, UB_KERNEL_REF_UNROLLED,
#if defined(__aarch64__) || defined(_M_ARM64)
    UB_KERNEL_NEON,
#endif
  };
  const size_t NK = sizeof(kernels)/sizeof(kernels[0]);

  /* Lengths: every value 0..300 (covers all sub-block, block, and
   * multi-block boundaries and their neighbors), plus some large ones. */
  const size_t big_lens[] = { 512, 1000, 4096, 16384, 65536 };
  const size_t MAXBUF = 65536;
  static uint8_t in[65536];
  for (size_t i = 0; i < MAXBUF; ++i) in[i] = rnd_byte();

  uint8_t persona[16];
  for (int i = 0; i < 16; ++i) persona[i] = rnd_byte();

  int total_fail = 0;

  for (size_t ki = 0; ki < NK; ++ki) {
    ub_kernel_id kid = kernels[ki];
    if (ub_force_kernel(kid) != 0) {
      printf("  FAIL could not force kernel %s\n", ub_kernel_name(kid));
      total_fail++; continue;
    }
    long checks = 0, fails = 0;

    for (int persel = 0; persel < 2; ++persel) {
      const uint8_t *pers = persel ? persona : NULL;

      /* All small lengths 0..300, every outlen 1,16,32,48,64. */
      for (size_t n = 0; n <= 300; ++n) {
        static const size_t olens[] = { 1, 16, 32, 48, 64 };
        for (size_t oi = 0; oi < 5; ++oi) {
          size_t olen = olens[oi];
          uint8_t got[64], want[64];
          ub_blake2b(got, olen, in, n, pers);
          ub_oracle_blake2b(want, olen, in, n, pers);
          checks++;
          if (memcmp(got, want, olen) != 0) { fails++;
            if (fails <= 3) printf("    %s mismatch n=%zu olen=%zu pers=%d\n",
                                   ub_kernel_name(kid), n, olen, persel); }
        }
      }
      /* Large multi-block messages at outlen 64. */
      for (size_t bi = 0; bi < sizeof(big_lens)/sizeof(big_lens[0]); ++bi) {
        size_t n = big_lens[bi];
        uint8_t got[64], want[64];
        ub_blake2b(got, 64, in, n, pers);
        ub_oracle_blake2b(want, 64, in, n, pers);
        checks++;
        if (memcmp(got, want, 64) != 0) { fails++;
          printf("    %s mismatch big n=%zu pers=%d\n", ub_kernel_name(kid), n, persel); }
      }
    }

    /* Chunked-update path: feed a 10000-byte message in odd-sized
     * chunks, must match a one-shot oracle hash. Exercises the update
     * buffering across block boundaries with this kernel. */
    {
      size_t n = 10000;
      ub_state S; ub_blake2b_init_personal(&S, 64, persona);
      size_t off = 0;
      while (off < n) {
        size_t chunk = 1 + (rnd_byte() % 97); /* 1..97 */
        if (off + chunk > n) chunk = n - off;
        ub_blake2b_update(&S, in + off, chunk);
        off += chunk;
      }
      uint8_t got[64], want[64];
      ub_blake2b_final(&S, got, 64);
      ub_oracle_blake2b(want, 64, in, n, persona);
      checks++;
      if (memcmp(got, want, 64) != 0) { fails++;
        printf("    %s chunked-update mismatch\n", ub_kernel_name(kid)); }
    }

    printf("  %s %-13s %ld checks, %ld fail\n",
           fails ? "FAIL" : "ok  ", ub_kernel_name(kid), checks, fails);
    total_fail += (fails != 0);
  }

  printf("=== %s ===\n", total_fail ? "STRESS FAILED" : "STRESS GREEN");
  return total_fail ? 1 : 0;
}
