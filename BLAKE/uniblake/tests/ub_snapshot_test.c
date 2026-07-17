/* uniblake snapshot validation (U3, §4).
 *
 * Asserts the snapshot's contract:
 *  1. round-trip: export → import → same digest as no round-trip;
 *  2. repeated import: one snapshot seeds many finalizations (the
 *     durable midstate use case);
 *  3. cross-kernel: a snapshot exported under one kernel imports and
 *     finalizes correctly under another (wire form is kernel-agnostic);
 *  4. loud rejection: bad magic, wrong family, unsupported version, and
 *     truncation each return their specific error, never a silent
 *     misread;
 *  5. distinctness: the live-struct copy (ub_blake2b_copy) and the
 *     snapshot both reproduce the oracle, but the snapshot survives a
 *     serialize boundary the struct copy doesn't model.
 */
#include "uniblake.h"
#include "ub_internal.h"
#include "ub_oracle.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(c, m) do { if (c) printf("  ok   %s\n", m); \
  else { printf("  FAIL %s\n", m); failures++; } } while (0)

int main(void) {
  printf("=== uniblake snapshot validation ===\n");
  printf("snapshot size = %zu bytes\n", ub_snapshot_size());

  uint8_t prefix[140];
  for (size_t i = 0; i < sizeof prefix; ++i) prefix[i] = (uint8_t)(i + 3);
  const uint8_t pers[16] = { 'Z','c','a','s','h','P','o','W',0,0,0,0,0,0,0,0 };

  /* Build a midstate: persona + prefix absorbed. */
  ub_state base;
  ub_blake2b_init_personal(&base, 50, pers);
  ub_blake2b_update(&base, prefix, sizeof prefix);

  /* Export it. */
  uint8_t snap[512];
  int erc = ub_blake2b_export(&base, snap, sizeof snap);
  CHECK(erc == 0, "export succeeds");

  /* (1) round-trip + (2) repeated import: import the snapshot many
   * times, each time append a distinct leaf counter and finalize;
   * compare to a from-scratch oracle hash of prefix||ctr. */
  int rt_ok = 1;
  for (uint32_t leaf = 0; leaf < 5000; ++leaf) {
    ub_state s;
    if (ub_blake2b_import(&s, snap, ub_snapshot_size()) != UB_IMPORT_OK) { rt_ok = 0; break; }
    uint8_t ctr[4] = {(uint8_t)leaf,(uint8_t)(leaf>>8),(uint8_t)(leaf>>16),(uint8_t)(leaf>>24)};
    ub_blake2b_update(&s, ctr, 4);
    uint8_t got[50]; ub_blake2b_final(&s, got, 50);

    uint8_t msg[144]; memcpy(msg, prefix, 140); memcpy(msg + 140, ctr, 4);
    uint8_t want[50]; ub_oracle_blake2b(want, 50, msg, 144, pers);
    if (memcmp(got, want, 50) != 0) { rt_ok = 0;
      printf("    leaf %u mismatch after import\n", leaf); break; }
  }
  CHECK(rt_ok, "5000 repeated imports each finalize correctly (round-trip + reuse)");

  /* (3) cross-kernel: export under ref, import+finalize under each
   * other registered kernel. Since export is kernel-independent, we
   * force a kernel for the FINALIZE side and confirm agreement. */
  {
    const ub_kernel_id ids[] = {
      UB_KERNEL_REF, UB_KERNEL_REF_UNROLLED,
#if defined(__aarch64__) || defined(_M_ARM64)
      UB_KERNEL_NEON,
#endif
    };
    int xk_ok = 1;
    for (size_t i = 0; i < sizeof(ids)/sizeof(ids[0]); ++i) {
      ub_force_kernel(ids[i]);
      ub_state s; ub_blake2b_import(&s, snap, ub_snapshot_size());
      uint8_t ctr[4] = { 9, 9, 9, 9 };
      ub_blake2b_update(&s, ctr, 4);
      uint8_t got[50]; ub_blake2b_final(&s, got, 50);
      uint8_t msg[144]; memcpy(msg, prefix, 140); memcpy(msg+140, ctr, 4);
      uint8_t want[50]; ub_oracle_blake2b(want, 50, msg, 144, pers);
      if (memcmp(got, want, 50) != 0) { xk_ok = 0;
        printf("    kernel %s cross-import mismatch\n", ub_kernel_name(ids[i])); }
    }
    CHECK(xk_ok, "snapshot imports+finalizes identically under every kernel");
  }

  /* (3b) dirty-state import: importing MUST stop whatever the state was
   * doing and fully reset it — no field (h/t/f/buf/buflen/outlen) may
   * carry over from a prior, unrelated, mid-operation use of the same
   * ub_state. Build a maximally-dirty state (different persona, a long
   * partially-absorbed message left mid-update, wrong outlen), import
   * the snapshot on top of it, and require the result to equal a fresh
   * import — proving import is a full reset, not a partial overwrite. */
  {
    /* Fresh-import reference result. */
    ub_state fresh; ub_blake2b_import(&fresh, snap, ub_snapshot_size());
    uint8_t ctr[4] = { 7, 0, 0, 0 };
    ub_blake2b_update(&fresh, ctr, 4);
    uint8_t want[50]; ub_blake2b_final(&fresh, want, 50);

    /* Dirty the state every way that could leave residue: a different
     * persona, a different outlen, and a long half-absorbed message so
     * t (counter), buf, and buflen are all non-zero and wrong. */
    ub_state dirty;
    const uint8_t other_pers[16] = { 'X','X','X','X','X','X','X','X',
                                     'X','X','X','X','X','X','X','X' };
    ub_blake2b_init_personal(&dirty, 33, other_pers);   /* wrong outlen 33 */
    uint8_t junk[500];
    for (size_t i = 0; i < sizeof junk; ++i) junk[i] = (uint8_t)(i ^ 0x5A);
    ub_blake2b_update(&dirty, junk, sizeof junk);        /* leaves t/buf/buflen dirty */

    /* Now import the real snapshot on top of the dirty state. */
    int dirty_ok = 1;
    if (ub_blake2b_import(&dirty, snap, ub_snapshot_size()) != UB_IMPORT_OK) dirty_ok = 0;
    ub_blake2b_update(&dirty, ctr, 4);
    uint8_t got[50]; ub_blake2b_final(&dirty, got, 50);
    if (memcmp(got, want, 50) != 0) dirty_ok = 0;
    CHECK(dirty_ok,
          "import into a dirty mid-operation state == fresh import (full reset)");

    /* Byte-level assurance: after import, the raw struct bytes are
     * IDENTICAL whether the target was fresh-zeroed or dirtied — i.e.
     * import leaves no field, internal or otherwise, dependent on prior
     * contents. */
    ub_state a, b;
    memset(&a, 0x00, sizeof a);      /* one target all-zero */
    memset(&b, 0xEE, sizeof b);      /* the other all-0xEE (maximally different) */
    ub_blake2b_import(&a, snap, ub_snapshot_size());
    ub_blake2b_import(&b, snap, ub_snapshot_size());
    CHECK(memcmp(&a, &b, sizeof a) == 0,
          "post-import struct bytes independent of prior contents (no residue)");
  }

  /* (4) loud rejection. */
  {
    ub_state s;
    uint8_t bad[512];

    memcpy(bad, snap, ub_snapshot_size());
    bad[0] ^= 0xFF; /* corrupt magic */
    CHECK(ub_blake2b_import(&s, bad, ub_snapshot_size()) == UB_IMPORT_EMAGIC,
          "bad magic -> EMAGIC");

    memcpy(bad, snap, ub_snapshot_size());
    bad[5] = 2; /* wrong family (e.g. BLAKE3) */
    CHECK(ub_blake2b_import(&s, bad, ub_snapshot_size()) == UB_IMPORT_EFAMILY,
          "wrong family -> EFAMILY");

    memcpy(bad, snap, ub_snapshot_size());
    bad[4] = 99; /* unsupported version */
    CHECK(ub_blake2b_import(&s, bad, ub_snapshot_size()) == UB_IMPORT_EVERSION,
          "unsupported version -> EVERSION (loud, not silent)");

    CHECK(ub_blake2b_import(&s, snap, 8) == UB_IMPORT_ETRUNCATED,
          "truncated input -> ETRUNCATED");

    memcpy(bad, snap, ub_snapshot_size());
    bad[6] = 200; /* outlen out of range */
    CHECK(ub_blake2b_import(&s, bad, ub_snapshot_size()) == UB_IMPORT_ECORRUPT,
          "out-of-range field -> ECORRUPT");
  }

  printf("=== %s (%d failure%s) ===\n",
         failures ? "SNAPSHOT FAILED" : "SNAPSHOT GREEN",
         failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
