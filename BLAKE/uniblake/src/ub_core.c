/* uniblake core — streaming API, dispatch, registration, self-test.
 *
 * Ties together: the opaque state, the compress-kernel table (§2), the
 * runtime probe (R3), UB_FORCE_IMPL (R7), and the oracle self-test
 * gate (§5). The streaming logic (update buffering, counter, padding,
 * finalization) is the reference's, adapted to call compress through
 * the selected kernel over whole runs of blocks. Provenance:
 * ../PROVENANCE.md.
 */
#include "uniblake.h"
#include "ub_internal.h"
#include "ub_probe.h"
#include "ub_oracle.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* --- opaque-state size/alignment (anti-pattern #1 defense) --- */
size_t ub_state_size(void)  { return sizeof(struct ub_state); }
size_t ub_state_align(void) { return _Alignof(struct ub_state); }

/* --- kernel registry --- */
typedef struct {
  ub_kernel_id   id;
  const char    *name;
  ub_compress_fn fn;
  int            available; /* compiled in on this build/target */
} ub_kernel_entry;

static ub_kernel_entry g_kernels[] = {
  { UB_KERNEL_REF,          "ref",          ub_compress_ref,          1 },
  { UB_KERNEL_REF_UNROLLED, "ref_unrolled", ub_compress_ref_unrolled, 1 },
};
static const size_t g_nkernels = sizeof(g_kernels) / sizeof(g_kernels[0]);

static ub_compress_fn g_active_fn   = 0;
static ub_kernel_id   g_active_id   = UB_KERNEL_AUTO;
static int            g_initialized = 0;

const char *ub_kernel_name(ub_kernel_id id) {
  for (size_t i = 0; i < g_nkernels; ++i)
    if (g_kernels[i].id == id) return g_kernels[i].name;
  return "(unknown)";
}

static ub_kernel_entry *find_kernel(ub_kernel_id id) {
  for (size_t i = 0; i < g_nkernels; ++i)
    if (g_kernels[i].id == id && g_kernels[i].available)
      return &g_kernels[i];
  return 0;
}

/* --- probe → default kernel choice ---
 * PoC has only scalar kernels, so AUTO resolves to ref regardless of
 * the probe result; the probe is still run and reported (proving
 * detection works) and is the hook U2's SIMD selection plugs into. */
static ub_kernel_id choose_kernel_from_cpu(void) {
  ub_cpu_features f = ub_detect_cpu();
  (void)f; /* no SIMD kernels yet; U2 consults f here */
  return UB_KERNEL_REF;
}

/* --- the oracle self-test gate (§5) ---
 * Every available kernel must reproduce the oracle's bytes across a
 * fixed battery, or it is rejected. Returns 0 if all pass, else
 * -(id) of the first failing kernel. A forced kernel that fails here
 * is refused by ub_force_kernel — forcing never bypasses the gate. */
static int selftest_kernel(ub_kernel_entry *k);

int ub_selftest(void) {
  for (size_t i = 0; i < g_nkernels; ++i) {
    if (!g_kernels[i].available) continue;
    if (selftest_kernel(&g_kernels[i]) != 0)
      return -(int)g_kernels[i].id;
  }
  return 0;
}

/* Forward decls of the streaming primitives used by the self-test. */
static int ub_init_with(struct ub_state *S, size_t outlen,
                        const uint8_t *personal, ub_compress_fn fn);
static int ub_update_with(struct ub_state *S, const void *in, size_t inlen,
                          ub_compress_fn fn);
static int ub_final_with(struct ub_state *S, void *out, size_t outlen,
                         ub_compress_fn fn);

/* Validate one kernel against the pristine oracle backend across a
 * battery spanning: empty, sub-block, exact block, multi-block,
 * unaligned tail, and personalized inputs (§1d oracle type 1). */
static int selftest_kernel(ub_kernel_entry *k) {
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
      if (ub_init_with(&S, 64, pers, k->fn) != 0) return -1;
      if (ub_update_with(&S, in, n, k->fn) != 0) return -1;
      if (ub_final_with(&S, got, 64, k->fn) != 0) return -1;

      ub_oracle_blake2b(want, 64, in, n, pers);
      if (memcmp(got, want, 64) != 0) return -1;
    }
  }
  return 0;
}

/* --- one-time init: probe, gate, select --- */
static int ensure_init(void) {
  if (g_initialized) return 0;

  /* Gate first: no kernel is usable until it passes the oracle. */
  int st = ub_selftest();
  if (st != 0) {
    fprintf(stderr, "uniblake: self-test FAILED for kernel %s\n",
            ub_kernel_name((ub_kernel_id)(-st)));
    return -1;
  }

  /* UB_FORCE_IMPL env override (R7). */
  ub_kernel_id chosen = UB_KERNEL_AUTO;
  const char *forced = getenv("UB_FORCE_IMPL");
  if (forced && *forced) {
    for (size_t i = 0; i < g_nkernels; ++i)
      if (strcmp(forced, g_kernels[i].name) == 0 && g_kernels[i].available)
        chosen = g_kernels[i].id;
    if (chosen == UB_KERNEL_AUTO)
      fprintf(stderr, "uniblake: UB_FORCE_IMPL=%s not available; using auto\n",
              forced);
  }
  if (chosen == UB_KERNEL_AUTO) chosen = choose_kernel_from_cpu();

  ub_kernel_entry *k = find_kernel(chosen);
  if (!k) return -1;
  g_active_fn   = k->fn;
  g_active_id   = k->id;
  g_initialized = 1;
  return 0;
}

ub_kernel_id ub_active_kernel(void) {
  if (ensure_init() != 0) return UB_KERNEL_AUTO;
  return g_active_id;
}

int ub_force_kernel(ub_kernel_id id) {
  ub_kernel_entry *k = find_kernel(id);
  if (!k) return -1;
  /* Forcing selects — but never bypasses the gate (§5). */
  if (selftest_kernel(k) != 0) return -1;
  g_active_fn   = k->fn;
  g_active_id   = k->id;
  g_initialized = 1;
  return 0;
}

/* --- streaming primitives (kernel passed explicitly) --- */

static void set_lastblock(struct ub_state *S) { S->f[0] = (uint64_t)-1; }
static int  is_lastblock(const struct ub_state *S) { return S->f[0] != 0; }
static void inc_counter(struct ub_state *S, uint64_t inc) {
  S->t[0] += inc;
  S->t[1] += (S->t[0] < inc);
}

static int ub_init_with(struct ub_state *S, size_t outlen,
                        const uint8_t *personal, ub_compress_fn fn) {
  (void)fn;
  if (!outlen || outlen > UB_BLAKE2B_OUTBYTES) return -1;

  /* Build the 64-byte parameter block, xor into IV. Layout per RFC
   * 7693 §2.8: digest_length, key_length, fanout, depth at bytes 0..3;
   * personal at bytes 48..63. Sequential-mode (fanout=depth=1). */
  uint8_t P[64];
  memset(P, 0, sizeof P);
  P[0] = (uint8_t)outlen; /* digest_length */
  P[1] = 0;               /* key_length */
  P[2] = 1;               /* fanout */
  P[3] = 1;               /* depth */
  if (personal) memcpy(P + 48, personal, 16);

  memset(S, 0, sizeof *S);
  for (int i = 0; i < 8; ++i)
    S->h[i] = ub_blake2b_IV[i] ^ ub_load64(P + i * 8);
  S->outlen = outlen;
  return 0;
}

static int ub_update_with(struct ub_state *S, const void *pin, size_t inlen,
                          ub_compress_fn fn) {
  const uint8_t *in = (const uint8_t *)pin;
  if (inlen == 0) return 0;

  size_t left = S->buflen;
  size_t fill = UB_BLOCKBYTES - left;
  if (inlen > fill) {
    S->buflen = 0;
    memcpy(S->buf + left, in, fill);
    inc_counter(S, UB_BLOCKBYTES);
    fn(S, S->buf, 1);
    in += fill; inlen -= fill;
    /* Compress whole blocks, holding the final partial for finalize.
     * Counter must advance per block, so drive them one run at a time
     * with per-block increments (matches the reference exactly). */
    while (inlen > UB_BLOCKBYTES) {
      inc_counter(S, UB_BLOCKBYTES);
      fn(S, in, 1);
      in += UB_BLOCKBYTES; inlen -= UB_BLOCKBYTES;
    }
  }
  memcpy(S->buf + S->buflen, in, inlen);
  S->buflen += inlen;
  return 0;
}

static int ub_final_with(struct ub_state *S, void *out, size_t outlen,
                         ub_compress_fn fn) {
  if (out == NULL || outlen < S->outlen) return -1;
  if (is_lastblock(S)) return -1;

  inc_counter(S, S->buflen);
  set_lastblock(S);
  memset(S->buf + S->buflen, 0, UB_BLOCKBYTES - S->buflen);
  fn(S, S->buf, 1);

  uint8_t buffer[64] = {0};
  for (int i = 0; i < 8; ++i) ub_store64(buffer + i * 8, S->h[i]);
  memcpy(out, buffer, S->outlen);
  return 0;
}

/* --- public streaming API (uses the active kernel) --- */

int ub_blake2b_init(ub_state *S, size_t outlen) {
  return ub_blake2b_init_personal(S, outlen, 0);
}

int ub_blake2b_init_personal(ub_state *S, size_t outlen,
                             const uint8_t personal[UB_BLAKE2B_PERSONALBYTES]) {
  if (ensure_init() != 0) return -1;
  return ub_init_with(S, outlen, personal, g_active_fn);
}

int ub_blake2b_update(ub_state *S, const void *in, size_t inlen) {
  return ub_update_with(S, in, inlen, g_active_fn);
}

int ub_blake2b_final(ub_state *S, void *out, size_t outlen) {
  return ub_final_with(S, out, outlen, g_active_fn);
}

int ub_blake2b_copy(ub_state *dst, const ub_state *src) {
  if (!dst || !src) return -1;
  memcpy(dst, src, sizeof *dst);
  return 0;
}

int ub_blake2b(void *out, size_t outlen, const void *in, size_t inlen,
               const uint8_t personal[UB_BLAKE2B_PERSONALBYTES]) {
  ub_state S;
  if (ub_blake2b_init_personal(&S, outlen, personal) != 0) return -1;
  if (ub_blake2b_update(&S, in, inlen) != 0) return -1;
  return ub_blake2b_final(&S, out, outlen);
}
