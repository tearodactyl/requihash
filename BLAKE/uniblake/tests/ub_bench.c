/* uniblake dispatch-cost benchmark (U1 finding 1).
 *
 * Isolates the ONE variable the §2 concern is about: routing each
 * compress through a function pointer (g_active_fn) vs. calling the
 * kernel directly. Everything else is held identical.
 *
 *   (A) DIRECT   — ub_compress_ref(S, blk, 1) called by name.
 *   (B) INDIRECT — (*fp)(S, blk, 1) where fp = ub_compress_ref, and fp
 *                  is laundered through a volatile sink so the compiler
 *                  cannot devirtualize it (this is the real shipped
 *                  situation: the pointer is chosen at runtime by the
 *                  probe, so the compiler can't know it points at ref).
 *
 * Both call the SAME library symbol across the SAME TU boundary, so the
 * only delta is the indirection. This is the honest measure of the
 * dispatch tax per compress. Result is reported per-compress and, for
 * context, scaled to the Equihash leaf (~2 compresses/leaf: one for the
 * prefix-sharing block already done in the midstate, one for the
 * padded final block — the per-leaf marginal cost is ~1 compress).
 *
 * Earlier version of this bench compared the full streaming wrapper
 * two ways and got a confounded result (the hand-written static
 * wrappers optimized differently from the library path); this version
 * avoids that by benchmarking the compress call in isolation.
 */
#include "uniblake.h"
#include "ub_internal.h"
#include "ub_oracle.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <stdlib.h>

#define ITERS 20000000u   /* compress calls per rep */
#define REPS  9

static double now_ns(void) {
  struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}
static int cmp_d(const void *a, const void *b) {
  double x = *(const double *)a, y = *(const double *)b;
  return (x > y) - (x < y);
}

/* A volatile pointer the compiler must load at runtime — models the
 * probe-selected g_active_fn, defeating devirtualization. */
static volatile ub_compress_fn g_fp;

int main(void) {
  /* One initialized state + one block; we compress repeatedly. The
   * state mutates, which is fine — we measure call cost, not a digest.
   * Both paths start from an identical fresh state each rep. */
  uint8_t block[UB_BLOCKBYTES];
  for (size_t i = 0; i < sizeof block; ++i) block[i] = (uint8_t)(i * 7 + 1);

  g_fp = ub_compress_ref;

  printf("=== uniblake dispatch-cost bench (compress-isolated) ===\n");
  printf("iters/rep=%u reps=%d\n\n", ITERS, REPS);

  double dir[REPS], ind[REPS];
  volatile uint64_t sink = 0;

  for (int r = 0; r < REPS; ++r) {
    struct ub_state S; memset(&S, 0, sizeof S);
    for (int i = 0; i < 8; ++i) S.h[i] = ub_blake2b_IV[i];
    double t0 = now_ns();
    for (uint32_t i = 0; i < ITERS; ++i) ub_compress_ref(&S, block, 1);
    dir[r] = now_ns() - t0;
    sink ^= S.h[0];

    struct ub_state T; memset(&T, 0, sizeof T);
    for (int i = 0; i < 8; ++i) T.h[i] = ub_blake2b_IV[i];
    ub_compress_fn fp = g_fp;   /* runtime-loaded, opaque to optimizer */
    t0 = now_ns();
    for (uint32_t i = 0; i < ITERS; ++i) fp(&T, block, 1);
    ind[r] = now_ns() - t0;
    sink ^= T.h[0];
  }
  (void)sink;

  qsort(dir, REPS, sizeof(double), cmp_d);
  qsort(ind, REPS, sizeof(double), cmp_d);
  double dmed = dir[REPS/2], imed = ind[REPS/2];
  double dmin = dir[0],      imin = ind[0];

  printf("path      min(ns/compress)  median(ns/compress)\n");
  printf("direct    %15.3f  %18.3f\n", dmin/ITERS, dmed/ITERS);
  printf("indirect  %15.3f  %18.3f\n", imin/ITERS, imed/ITERS);
  printf("\ndispatch tax (median): %+.3f ns/compress  (%.2f%%)\n",
         (imed-dmed)/ITERS, 100.0*(imed-dmed)/dmed);
  printf("dispatch tax (min):    %+.3f ns/compress  (%.2f%%)\n",
         (imin-dmin)/ITERS, 100.0*(imin-dmin)/dmin);
  printf("\nfor scale: one BLAKE2b compress is 12 rounds; the leaf\n");
  printf("marginal cost is ~1 compress, so multiply by ~1 for ns/leaf.\n");
  return 0;
}
