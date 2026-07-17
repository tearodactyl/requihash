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
#include <assert.h>

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
#if defined(__aarch64__) || defined(_M_ARM64)
  { UB_KERNEL_NEON,         "neon",         ub_compress_neon,         1 },
#endif
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
 * AUTO consults the probe, but availability alone does NOT make a SIMD
 * kernel the default — it must be measured to win on THIS core (§1c:
 * "beats or ties portable on its hardware, else stays off"). And "its
 * hardware" means the specific microarchitecture, not just the ISA:
 * the same NEON code is faster than scalar on some aarch64 cores and
 * slower on others (Platforms.md §5 — Cortex-A53 vs A57; M-series vs
 * A9). So selection consults the full ub_cpu_info (vendor + generation
 * coordinates), not merely the ISA flags.
 *
 * The selection model is an explicit **per-microarchitecture win-list**:
 * a SIMD kernel is auto-selected only where it has been MEASURED to win
 * on that class of core. Everywhere else, the portable scalar `ref` is
 * the default — the safe, never-slower floor.
 *
 * MEASURED so far (U2 kbench): Apple M-series (strong scalar) → NEON is
 * 0.55–0.70x, a LOSS, so scalar wins. No aarch64 core is yet on the
 * NEON win-list (weaker in-order cores like Cortex-A53 are the expected
 * first entries, but not measured by us — do not add on expectation).
 *
 * A chosen kernel that failed the gate never reaches here — ensure_init
 * runs the gate first. */
static ub_kernel_id choose_kernel_from_cpu(void) {
  ub_cpu_features f = ub_detect_cpu();
  ub_cpu_info     ci = ub_detect_cpu_info();
  (void)f;

#if defined(__aarch64__) || defined(_M_ARM64)
  /* NEON win-list (empty so far). Example of the intended shape, kept
   * as a comment until a real measurement fills it:
   *   if (ci.vendor == UB_VENDOR_ARM && is_weak_inorder(ci.arm_part))
   *       return UB_KERNEL_NEON;
   * Apple M-series is explicitly NOT on it (measured loss). */
  (void)ci;
#endif
  /* x86 AVX2/SSE4.1 kernels (U2 continued) will consult f.avx2/f.sse41
   * and ci (family/model) here once written + measured. */
  return UB_KERNEL_REF;
}

/* --- the oracle self-test gate (§5) ---
 * Every available kernel must reproduce the oracle's bytes across the
 * shared battery (ub_kernel_matches_oracle, ub_selftest.c), or it is
 * rejected. Returns 0 if all pass, else -(id) of the first failing
 * kernel. A forced kernel that fails is refused by ub_force_kernel —
 * forcing never bypasses the gate. */
int ub_selftest(void) {
  for (size_t i = 0; i < g_nkernels; ++i) {
    if (!g_kernels[i].available) continue;
    if (ub_kernel_matches_oracle(g_kernels[i].fn) != 0)
      return -(int)g_kernels[i].id;
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
  if (ub_kernel_matches_oracle(k->fn) != 0) return -1;
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

int ub_init_with(struct ub_state *S, size_t outlen,
                 const uint8_t *personal, ub_compress_fn fn) {
  (void)fn;
  if (!outlen || outlen > UB_BLAKE2B_OUTBYTES) return -1;

  /* Build the 64-byte parameter block, xor into IV. Layout per RFC
   * 7693 §2.8: digest_length, key_length, fanout, depth at bytes 0..3;
   * personal at bytes 48..63. Sequential-mode (fanout=depth=1). */
  uint8_t P[64];
  memset(P, 0, sizeof P);       /* bounded: fixed 64-byte local */
  P[0] = (uint8_t)outlen; /* digest_length */
  P[1] = 0;               /* key_length */
  P[2] = 1;               /* fanout */
  P[3] = 1;               /* depth */
  /* personal is exactly 16 B by contract; the 16-byte copy lands at
   * P[48..63], inside the 64-byte block. */
  if (personal) memcpy(P + 48, personal, 16);

  memset(S, 0, sizeof *S);
  for (int i = 0; i < 8; ++i)
    S->h[i] = ub_blake2b_IV[i] ^ ub_load64(P + i * 8);
  S->outlen = outlen;
  return 0;
}

int ub_update_with(struct ub_state *S, const void *pin, size_t inlen,
                   ub_compress_fn fn) {
  const uint8_t *in = (const uint8_t *)pin;
  if (inlen == 0) return 0;

  size_t left = S->buflen;
  /* Invariant: buflen never exceeds the block size between calls.
   * asserts compile out under -DNDEBUG (release); in a debug build a
   * broken invariant trips here instead of silently overflowing buf. */
  assert(left <= UB_BLOCKBYTES);
  size_t fill = UB_BLOCKBYTES - left;
  if (inlen > fill) {
    S->buflen = 0;
    /* bounded: left + fill == UB_BLOCKBYTES, so this fills buf exactly
     * to its end and no further. */
    assert(left + fill == UB_BLOCKBYTES);
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
  /* bounded: after the branch above, inlen <= UB_BLOCKBYTES and
   * buflen == 0 (if the branch ran) or the original left (if it did
   * not, in which case inlen <= fill), so buflen + inlen <= 128 — the
   * copy stays inside buf. This is the one runtime-sized copy the
   * public API does not pre-check, so it is asserted. */
  assert(S->buflen + inlen <= UB_BLOCKBYTES);
  memcpy(S->buf + S->buflen, in, inlen);
  S->buflen += inlen;
  return 0;
}

int ub_final_with(struct ub_state *S, void *out, size_t outlen,
                  ub_compress_fn fn) {
  /* Public bounds contract: out must be non-NULL and the caller's
   * buffer at least S->outlen bytes. Violations return -1 (caller
   * error), NOT an assert — these are external-input checks, kept in
   * release builds. */
  if (out == NULL || outlen < S->outlen) return -1;
  if (is_lastblock(S)) return -1;

  inc_counter(S, S->buflen);
  set_lastblock(S);
  /* bounded: buflen <= 128 (update's invariant), so the pad length
   * 128-buflen is in [0,128] and the memset stays inside buf. */
  assert(S->buflen <= UB_BLOCKBYTES);
  memset(S->buf + S->buflen, 0, UB_BLOCKBYTES - S->buflen);
  fn(S, S->buf, 1);

  uint8_t buffer[64] = {0};
  for (int i = 0; i < 8; ++i) ub_store64(buffer + i * 8, S->h[i]);
  /* bounded: outlen <= 64 (enforced at init) and buffer is 64 B, and
   * the caller's out was checked >= S->outlen above. */
  assert(S->outlen <= UB_BLAKE2B_OUTBYTES);
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
  /* bounded: exact struct-sized copy (the midstate clone). */
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
