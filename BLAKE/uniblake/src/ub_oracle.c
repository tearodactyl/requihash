/* uniblake oracle backend — wraps the UNTOUCHED vendored reference.
 *
 * The vendored blake2b-ref.c is compiled here verbatim (via #include),
 * with its public symbols prefixed to `ubref_` so they don't collide
 * with the uniblake core. No line of the reference is edited — the
 * renaming is done by macro before the include, which is a
 * build/platform-class action (§1d), not an algorithmic one: the bytes
 * the reference computes are identical. This preserves §1a invariant 1
 * (an untouched reference remains linked as the oracle).
 *
 * NATIVE_LITTLE_ENDIAN is defined by the build for the target; the
 * reference's load64/store64 are correct either way.
 */
#include "ub_oracle.h"

/* Rename the reference's public API into a private namespace. */
#define blake2b_init        ubref_blake2b_init
#define blake2b_init_key    ubref_blake2b_init_key
#define blake2b_init_param  ubref_blake2b_init_param
#define blake2b_update      ubref_blake2b_update
#define blake2b_final       ubref_blake2b_final
#define blake2b             ubref_blake2b
#define blake2bp_init       ubref_blake2bp_init
#define blake2bp_init_key   ubref_blake2bp_init_key
#define blake2bp_update     ubref_blake2bp_update
#define blake2bp_final      ubref_blake2bp_final
#define blake2bp            ubref_blake2bp
#define blake2              ubref_blake2

/* Pull in the vendored header + implementation, untouched. Include
 * paths (vendor dir) are supplied by CMake. */
#include "blake2.h"
#include "blake2b-ref.c"

/* Call the reference's ubref_-prefixed functions directly below.
 * (blake2b_state / blake2b_param are TYPE names from blake2.h, not
 * renamed, so they are used as-is.) */

int ub_oracle_blake2b(void *out, size_t outlen, const void *in, size_t inlen,
                      const uint8_t personal[16]) {
  if (!personal) {
    return ubref_blake2b(out, outlen, in, inlen, NULL, 0);
  }
  /* Personalized path: build a param block and use the streaming init. */
  blake2b_param P;
  memset(&P, 0, sizeof P);
  P.digest_length = (uint8_t)outlen;
  P.key_length    = 0;
  P.fanout        = 1;
  P.depth         = 1;
  memcpy(P.personal, personal, 16);

  blake2b_state S;
  if (ubref_blake2b_init_param(&S, &P) < 0) return -1;
  ubref_blake2b_update(&S, in, inlen);
  return ubref_blake2b_final(&S, out, outlen);
}
