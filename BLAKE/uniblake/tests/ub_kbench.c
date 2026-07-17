/* uniblake kernel benchmark (U2) — registered kernels head-to-head.
 *
 * Times each registered compress kernel on the Equihash leaf shape
 * (a shared 140-byte prefix midstate + a 4-byte per-leaf tail,
 * finalized to 50 bytes) by forcing each kernel and running the same
 * leaf loop. Reports ns/leaf (min + median over reps) and the NEON
 * speedup vs. scalar ref. Correctness is assumed here (the stress test
 * proves it); this is purely the performance picture that DECIDES
 * kernel selection (on the M4 it demoted NEON from the default — see
 * STATUS.md finding 3).
 */
#include "uniblake.h"
#include "ub_internal.h"
#include "ub_probe.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <stdlib.h>

#define PREFIX 140
#define OUT    50
#define LEAVES 300000u
#define REPS   9

static double now_ns(void) {
  struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec*1e9 + (double)ts.tv_nsec;
}
static int cmp_d(const void *a, const void *b) {
  double x=*(const double*)a, y=*(const double*)b; return (x>y)-(x<y);
}

static double bench_kernel(ub_kernel_id kid, const uint8_t *prefix,
                           const uint8_t *pers) {
  if (ub_force_kernel(kid) != 0) return -1;
  double t[REPS];
  volatile uint8_t sink = 0;
  for (int r = 0; r < REPS; ++r) {
    ub_state base; ub_blake2b_init_personal(&base, OUT, pers);
    ub_blake2b_update(&base, prefix, PREFIX);
    double t0 = now_ns();
    for (uint32_t leaf = 0; leaf < LEAVES; ++leaf) {
      ub_state s; ub_blake2b_copy(&s, &base);
      uint8_t ctr[4] = {(uint8_t)leaf,(uint8_t)(leaf>>8),(uint8_t)(leaf>>16),(uint8_t)(leaf>>24)};
      ub_blake2b_update(&s, ctr, 4);
      uint8_t out[OUT]; ub_blake2b_final(&s, out, OUT);
      sink ^= out[0];
    }
    t[r] = now_ns() - t0;
  }
  (void)sink;
  qsort(t, REPS, sizeof(double), cmp_d);
  return t[REPS/2]; /* median total ns */
}

int main(void) {
  uint8_t prefix[PREFIX];
  for (size_t i = 0; i < sizeof prefix; ++i) prefix[i] = (uint8_t)(i+1);
  const uint8_t pers[16] = { 'Z','c','a','s','h','P','o','W',0,0,0,0,0,0,0,0 };

  printf("=== uniblake kernel benchmark (leaf shape) ===\n");
  /* Tie every number to its exact machine (Platforms.md §6: a SIMD
   * figure is meaningless without the CPU it was measured on). */
  ub_cpu_info ci = ub_detect_cpu_info();
  printf("cpu: %s %s \"%s\"", ub_arch_name(ci.arch),
         ub_vendor_name(ci.vendor), ci.brand);
  if (ci.arch == UB_ARCH_X86_64)
    printf(" [family=%d model=0x%X stepping=%d]", ci.x86_family, ci.x86_model, ci.x86_stepping);
  printf("\nprefix=%d out=%d leaves=%u reps=%d\n\n", PREFIX, OUT, LEAVES, REPS);

  const ub_kernel_id ids[] = {
    UB_KERNEL_REF, UB_KERNEL_REF_UNROLLED,
#if defined(__aarch64__) || defined(_M_ARM64)
    UB_KERNEL_NEON,
#endif
  };
  double ref_ns = -1, neon_ns = -1;
  printf("kernel        ns/leaf\n");
  for (size_t i = 0; i < sizeof(ids)/sizeof(ids[0]); ++i) {
    double med = bench_kernel(ids[i], prefix, pers);
    printf("%-12s  %7.2f\n", ub_kernel_name(ids[i]), med/LEAVES);
    if (ids[i] == UB_KERNEL_REF) ref_ns = med;
#if defined(__aarch64__) || defined(_M_ARM64)
    if (ids[i] == UB_KERNEL_NEON) neon_ns = med;
#endif
  }
  if (ref_ns > 0 && neon_ns > 0)
    printf("\nNEON speedup vs ref (leaf shape): %.2fx\n", ref_ns/neon_ns);

  /* Bulk shape: one long message hashed repeatedly. This is where SIMD
   * lane parallelism has the best chance (throughput, not per-leaf
   * latency). If NEON loses here too on this core, it loses outright. */
  printf("\n=== bulk shape (1 MiB message) ===\n");
  const size_t BN = 1u << 20;
  static uint8_t big[1u << 20];
  for (size_t i = 0; i < BN; ++i) big[i] = (uint8_t)(i * 3 + 1);
  printf("kernel        MiB/s\n");
  double ref_b = -1, neon_b = -1;
  for (size_t i = 0; i < sizeof(ids)/sizeof(ids[0]); ++i) {
    if (ub_force_kernel(ids[i]) != 0) continue;
    double t[REPS]; volatile uint8_t sink = 0;
    for (int r = 0; r < REPS; ++r) {
      double t0 = now_ns();
      for (int rep = 0; rep < 20; ++rep) {
        uint8_t out[64]; ub_blake2b(out, 64, big, BN, pers); sink ^= out[0];
      }
      t[r] = now_ns() - t0;
    }
    (void)sink; qsort(t, REPS, sizeof(double), cmp_d);
    double secs = t[REPS/2] / 1e9;
    double mibps = (20.0 * BN / (1024.0*1024.0)) / secs;
    printf("%-12s  %7.1f\n", ub_kernel_name(ids[i]), mibps);
    if (ids[i] == UB_KERNEL_REF) ref_b = mibps;
#if defined(__aarch64__) || defined(_M_ARM64)
    if (ids[i] == UB_KERNEL_NEON) neon_b = mibps;
#endif
  }
  if (ref_b > 0 && neon_b > 0)
    printf("\nNEON speedup vs ref (bulk): %.2fx\n", neon_b/ref_b);
  return 0;
}
