/* uniblake PoC test + validation harness.
 *
 * Exercises all three §1d oracle types plus the dispatch/gate
 * machinery. Exit 0 = all green. Prints a summary so the PoC run is
 * legible. Built for both arm64 (real probe) and the cross-platform
 * targets (C3).
 */
#include "uniblake.h"
#include "ub_internal.h" /* in-tree test: full ub_state for stack use */
#include "ub_oracle.h"
#include "ub_probe.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (cond) { printf("  ok   %s\n", msg); } \
    else      { printf("  FAIL %s\n", msg); failures++; } \
  } while (0)

static void hex(const uint8_t *b, size_t n, char *out) {
  static const char *h = "0123456789abcdef";
  for (size_t i = 0; i < n; ++i) { out[2*i] = h[b[i]>>4]; out[2*i+1] = h[b[i]&15]; }
  out[2*n] = 0;
}

/* --- §1d type 2: published vector --- */
/* BLAKE2b-512("abc") — RFC 7693 / blake2.net known answer. */
static const char *ABC_512 =
  "ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d1"
  "7d87c5392aab792dc252d5de4533cc9518d38aa8dbf1925ab92386edd4009923";

static void test_abc_vector(void) {
  printf("[§1d type 2] published 'abc' KAT\n");
  uint8_t out[64]; char got[129];
  ub_blake2b(out, 64, "abc", 3, NULL);
  hex(out, 64, got);
  CHECK(strcmp(got, ABC_512) == 0, "BLAKE2b-512(\"abc\") matches RFC 7693");
}

/* --- §1d type 1: reference agreement, broad battery --- */
static void test_reference_agreement(void) {
  printf("[§1d type 1] byte-for-byte vs. pristine oracle\n");
  static const size_t lens[] = { 0, 1, 55, 63, 64, 65, 127, 128, 129, 255, 256, 1000, 4096 };
  uint8_t in[4096];
  for (size_t i = 0; i < sizeof in; ++i) in[i] = (uint8_t)(i * 191 + 13);
  const uint8_t persona[16] = "ZcashPoW\0\0\0\0\0\0\0"; /* Equihash-shaped persona */

  int all = 1;
  for (int p = 0; p < 2; ++p) {
    const uint8_t *pers = p ? persona : NULL;
    for (size_t li = 0; li < sizeof(lens)/sizeof(lens[0]); ++li) {
      size_t n = lens[li];
      for (size_t olen = 1; olen <= 64; olen += 21) { /* 1,22,43,64 */
        uint8_t got[64], want[64];
        ub_blake2b(got, olen, in, n, pers);
        ub_oracle_blake2b(want, olen, in, n, pers);
        if (memcmp(got, want, olen) != 0) { all = 0;
          printf("    mismatch len=%zu olen=%zu persona=%d\n", n, olen, p); }
      }
    }
  }
  CHECK(all, "active kernel agrees with oracle across len/outlen/persona battery");
}

/* --- output-bounds: the digest must touch ONLY outlen bytes ---
 * Pad a generous buffer with a sentinel (0xAA), hash into `outlen`
 * bytes, and require that every byte AT or BEYOND outlen is still the
 * sentinel — i.e. finalize never writes past the caller's requested
 * length. This catches an over-write (writing 64 when 50 was asked)
 * that a plain oracle-agreement check on the first outlen bytes would
 * miss. Exercised across the Requihash digest neighborhood and the
 * extremes. 0xAA (0b10101010) is chosen because it is not 0x00, not
 * 0xFF, and not a plausible digest byte pattern, so any stray write
 * shows up unambiguously. */
static void test_output_bounds(void) {
  printf("[bounds] finalize writes exactly outlen bytes, no more\n");
  uint8_t in[200];
  for (size_t i = 0; i < sizeof in; ++i) in[i] = (uint8_t)(i + 5);
  const uint8_t persona[16] = "ZcashPoW\0\0\0\0\0\0\0";

  static const size_t olens[] = { 1, 25, 48, 50, 54, 60, 63, 64 };
  int all = 1;
  for (int p = 0; p < 2; ++p) {
    const uint8_t *pers = p ? persona : NULL;
    for (size_t oi = 0; oi < sizeof(olens)/sizeof(olens[0]); ++oi) {
      size_t olen = olens[oi];
      uint8_t buf[128];
      memset(buf, 0xAA, sizeof buf);            /* sentinel-fill the whole buffer */
      ub_blake2b(buf, olen, in, sizeof in, pers);
      for (size_t j = olen; j < sizeof buf; ++j) /* everything past olen untouched? */
        if (buf[j] != 0xAA) { all = 0;
          printf("    over-write at olen=%zu byte=%zu (=0x%02X)\n", olen, j, buf[j]); }
    }
  }
  CHECK(all, "finalize touches only the requested outlen bytes (0xAA sentinel intact)");
}

/* --- §1d type 3: operational (midstate consumer path) --- */
static void test_operational_midstate(void) {
  printf("[§1d type 3] operational: midstate leaf pattern\n");
  /* Simulate the Equihash leaf pattern: hash a 140-byte prefix once,
   * copy the state, append a 4-byte counter per leaf, finalize. Cross-
   * check each leaf against a from-scratch oracle hash of prefix||ctr. */
  uint8_t prefix[140];
  for (size_t i = 0; i < sizeof prefix; ++i) prefix[i] = (uint8_t)(i + 1);
  const uint8_t persona[16] = "ZcashPoW\0\0\0\0\0\0\0";

  ub_state base;
  ub_blake2b_init_personal(&base, 50, persona);
  ub_blake2b_update(&base, prefix, sizeof prefix);

  int all = 1;
  for (uint32_t leaf = 0; leaf < 2048; ++leaf) {
    ub_state s;
    ub_blake2b_copy(&s, &base);           /* midstate reuse */
    uint8_t ctr[4] = { (uint8_t)leaf, (uint8_t)(leaf>>8),
                       (uint8_t)(leaf>>16), (uint8_t)(leaf>>24) };
    ub_blake2b_update(&s, ctr, 4);
    uint8_t got[50]; ub_blake2b_final(&s, got, 50);

    uint8_t msg[144]; memcpy(msg, prefix, 140); memcpy(msg + 140, ctr, 4);
    uint8_t want[50]; ub_oracle_blake2b(want, 50, msg, 144, persona);
    if (memcmp(got, want, 50) != 0) { all = 0; if (leaf < 4)
      printf("    leaf %u mismatch\n", leaf); }
  }
  CHECK(all, "2048 midstate-reuse leaves match from-scratch oracle");
}

/* --- R3/R7: probe, dispatch, forced-impl, gate --- */
static void test_dispatch(void) {
  printf("[R3 R7] probe / dispatch / forced-impl / gate\n");

  ub_cpu_features f = ub_detect_cpu();
  ub_cpu_info ci = ub_detect_cpu_info();
  printf("    cpu: %s %s \"%s\"\n", ub_arch_name(ci.arch),
         ub_vendor_name(ci.vendor), ci.brand);
  if (ci.arch == UB_ARCH_X86_64)
    printf("    x86 gen: family=%d model=0x%X stepping=%d\n",
           ci.x86_family, ci.x86_model, ci.x86_stepping);
  else if (ci.arch == UB_ARCH_AARCH64 && ci.arm_implementer)
    printf("    arm gen: implementer=0x%02x part=0x%03x\n",
           ci.arm_implementer, ci.arm_part);
  printf("    isa flags: neon=%d sse41=%d avx2=%d avx512f=%d sha_ni=%d\n",
         f.neon, f.sse41, f.avx2, f.avx512f, f.sha_ni);
#if defined(__aarch64__) || defined(_M_ARM64)
  CHECK(f.neon == 1, "arm64 probe reports NEON present");
  CHECK(ci.arch == UB_ARCH_AARCH64, "cpu-info reports aarch64 arch");
  CHECK(ci.brand[0] != 0 && strcmp(ci.brand, "(unknown)") != 0,
        "cpu-info reports a non-empty brand string");
#else
  CHECK(1, "probe ran (x86/other: SIMD validation deferred to real hw)");
  CHECK(ci.arch == UB_ARCH_X86_64, "cpu-info reports x86_64 arch");
#endif

  int st = ub_selftest();
  CHECK(st == 0, "self-test gate passes for all registered kernels");

  ub_kernel_id def = ub_active_kernel();
  printf("    auto-selected kernel: %s\n", ub_kernel_name(def));
  CHECK(def != UB_KERNEL_AUTO, "auto selection resolved to a concrete kernel");

  /* Force each kernel; verify it produces oracle-correct output. */
  const ub_kernel_id ids[] = {
    UB_KERNEL_REF, UB_KERNEL_REF_UNROLLED,
#if defined(__aarch64__) || defined(_M_ARM64)
    UB_KERNEL_NEON,
#endif
  };
  for (size_t i = 0; i < sizeof(ids)/sizeof(ids[0]); ++i) {
    int fk = ub_force_kernel(ids[i]);
    char label[64];
    snprintf(label, sizeof label, "force '%s' accepted (passed gate)",
             ub_kernel_name(ids[i]));
    CHECK(fk == 0 && ub_active_kernel() == ids[i], label);

    uint8_t got[64], want[64];
    ub_blake2b(got, 64, "the quick brown fox", 19, NULL);
    ub_oracle_blake2b(want, 64, "the quick brown fox", 19, NULL);
    snprintf(label, sizeof label, "forced '%s' output matches oracle",
             ub_kernel_name(ids[i]));
    CHECK(memcmp(got, want, 64) == 0, label);
  }
}

int main(void) {
  printf("=== uniblake PoC validation ===\n");
  printf("state size=%zu align=%zu\n\n", ub_state_size(), ub_state_align());
  test_abc_vector();
  test_reference_agreement();
  test_output_bounds();
  test_operational_midstate();
  test_dispatch();
  printf("\n=== %s (%d failure%s) ===\n",
         failures ? "FAILED" : "ALL GREEN", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
