/* This crate's own accessors over the vendored reference (codes directly
 * to the modern distribution; the vendored files stay unmodified). Keeps
 * Rust layout-agnostic: the state is an opaque byte buffer whose size the
 * C side reports, so a vendor update can never silently desynchronize a
 * duplicated struct layout on the Rust side. */
#include "blake2.h"
#include <string.h>

size_t blake2ref_state_size(void) { return sizeof(blake2b_state); }

int blake2ref_init_personal(void *state, size_t outlen,
                            const unsigned char personal[16]) {
  blake2b_param P;
  memset(&P, 0, sizeof(P));
  if (outlen == 0 || outlen > BLAKE2B_OUTBYTES) return -1;
  P.digest_length = (uint8_t)outlen;
  P.fanout = 1;
  P.depth = 1;
  if (personal) memcpy(P.personal, personal, BLAKE2B_PERSONALBYTES);
  return blake2b_init_param((blake2b_state *)state, &P);
}

int blake2ref_update(void *state, const unsigned char *in, size_t inlen) {
  return blake2b_update((blake2b_state *)state, in, inlen);
}

int blake2ref_final(void *state, unsigned char *out, size_t outlen) {
  return blake2b_final((blake2b_state *)state, out, outlen);
}
