/* uniblake state snapshot — versioned export/import (§4, U3).
 *
 * The DISTINCTION this file exists to enforce (UniBlake.md §4):
 *
 *   - `struct ub_state` is the LIVE internal structure: free to change
 *     with the implementation, layout is nobody's business, opaque to
 *     callers.
 *   - the SNAPSHOT is a versioned, format-stable WIRE shape with
 *     historical-format expectations: a header (magic + format version
 *     + hash family) followed by the canonical BLAKE2b midstate in a
 *     fixed little-endian encoding. Old snapshot versions must remain
 *     importable, or be rejected loudly with a version error — never
 *     silently misread.
 *
 * Import is the inverse and is kernel-agnostic: a snapshot exported on
 * one build imports on another (different kernel, within version
 * rules), because the wire form is the mathematical state, not the
 * in-memory struct.
 *
 * Use case: the midstate pattern made durable — export a
 * prefix-absorbed state once, import it repeatedly to seed many
 * finalizations, including across processes/persistence.
 */
#include "uniblake.h"
#include "ub_internal.h"

#include <string.h>

/* Wire layout, version 1 (all integers little-endian):
 *   off  size  field
 *   0    4     magic "UBS1" (0x55 0x42 0x53 0x31)
 *   4    1     format_version (currently 1)
 *   5    1     hash_family (1 = BLAKE2b)  [§2a: distinguishes BLAKE3]
 *   6    1     outlen (1..64)
 *   7    1     buflen (0..128)
 *   8    64    h[8]  (8 × u64 LE)
 *   72   16    t[2]  (2 × u64 LE)
 *   88   16    f[2]  (2 × u64 LE)
 *   104  128   buf[128]
 *   = 232 bytes total.
 * The header's version+family are FIRST so any reader can reject an
 * incompatible snapshot before touching the body.
 *
 * ARCHITECTURE-INDEPENDENCE (the size_t point). The live struct's
 * buflen/outlen are `size_t` — 8 bytes on LP64/LLP64 (64-bit), 4 bytes
 * on ILP32 (32-bit ARM, wasm, many embedded) — and the struct carries
 * padding the C implementation chooses, so `sizeof(ub_state)` and its
 * field offsets are NOT portable across ABIs. The snapshot deliberately
 * does not serialize the struct: it writes buflen/outlen as fixed
 * single bytes (0..128 and 1..64 both fit in a u8) and h/t/f as fixed
 * little-endian u64s. So a snapshot exported on a 64-bit host imports
 * byte-identically on a 32-bit host and vice-versa — the wire form is
 * defined in fixed-width, endianness-pinned bytes, never in
 * `size_t`/padding. This is exactly why the snapshot exists as a
 * separate versioned format rather than a raw `memcpy` of the struct
 * (a struct dump would be unreadable across ABIs and silently wrong). */
#define UBS_MAGIC0 0x55
#define UBS_MAGIC1 0x42
#define UBS_MAGIC2 0x53
#define UBS_MAGIC3 0x31
#define UBS_VERSION 1
#define UBS_FAMILY_BLAKE2B 1
#define UBS_V1_SIZE 232

size_t ub_snapshot_size(void) { return UBS_V1_SIZE; }

int ub_blake2b_export(const ub_state *S, uint8_t *out, size_t outcap) {
  if (!S || !out || outcap < UBS_V1_SIZE) return -1;
  uint8_t *p = out;
  p[0] = UBS_MAGIC0; p[1] = UBS_MAGIC1; p[2] = UBS_MAGIC2; p[3] = UBS_MAGIC3;
  p[4] = UBS_VERSION;
  p[5] = UBS_FAMILY_BLAKE2B;
  p[6] = (uint8_t)S->outlen;
  p[7] = (uint8_t)S->buflen;
  p += 8;
  for (int i = 0; i < 8; ++i) { ub_store64(p, S->h[i]); p += 8; }
  for (int i = 0; i < 2; ++i) { ub_store64(p, S->t[i]); p += 8; }
  for (int i = 0; i < 2; ++i) { ub_store64(p, S->f[i]); p += 8; }
  memcpy(p, S->buf, UB_BLOCKBYTES); p += UB_BLOCKBYTES;
  return 0;
}

/* Import result codes let a caller distinguish "not ours" from
 * "ours but a version I don't support" — the loud-rejection contract. */
int ub_blake2b_import(ub_state *S, const uint8_t *in, size_t inlen) {
  if (!S || !in) return UB_IMPORT_EBADARG;
  if (inlen < 8) return UB_IMPORT_ETRUNCATED;
  if (in[0] != UBS_MAGIC0 || in[1] != UBS_MAGIC1 ||
      in[2] != UBS_MAGIC2 || in[3] != UBS_MAGIC3)
    return UB_IMPORT_EMAGIC;
  uint8_t version = in[4];
  uint8_t family  = in[5];
  if (family != UBS_FAMILY_BLAKE2B) return UB_IMPORT_EFAMILY;
  if (version != UBS_VERSION)       return UB_IMPORT_EVERSION;
  if (inlen < UBS_V1_SIZE)          return UB_IMPORT_ETRUNCATED;

  uint8_t outlen = in[6];
  uint8_t buflen = in[7];
  if (outlen < 1 || outlen > UB_BLAKE2B_OUTBYTES) return UB_IMPORT_ECORRUPT;
  if (buflen > UB_BLOCKBYTES)                     return UB_IMPORT_ECORRUPT;

  /* All reads below are inside `in` because inlen >= UBS_V1_SIZE (232)
   * was checked above: 8 header + 64 + 16 + 16 + 128 = 232. */
  const uint8_t *p = in + 8;
  memset(S, 0, sizeof *S);  /* full reset — see the residue tests */
  for (int i = 0; i < 8; ++i) { S->h[i] = ub_load64(p); p += 8; }
  for (int i = 0; i < 2; ++i) { S->t[i] = ub_load64(p); p += 8; }
  for (int i = 0; i < 2; ++i) { S->f[i] = ub_load64(p); p += 8; }
  /* bounded: fixed 128-byte copy; p is at in+104, and in has >= 232. */
  memcpy(S->buf, p, UB_BLOCKBYTES);
  S->outlen = outlen;   /* validated 1..64 above */
  S->buflen = buflen;   /* validated <=128 above */
  return UB_IMPORT_OK;
}
