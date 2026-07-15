// Implementation of blake2b_glue.h, the only translation unit that includes
// the reference BLAKE2b header (~/Work/ZK/ZKs/blake2-reference/ref/blake2.h,
// Samuel Neves, CC0) -- kept isolated from equi_miner.c's own compilation
// unit so the vendored blake2b.h's `BLAKE2bState`/function-pointer names
// never collide with the reference implementation's own names.

#include "blake2b_glue.h"
#include "blake2.h"

#include <stdlib.h>
#include <string.h>

struct BLAKE2bState {
  blake2b_state inner;
  size_t digest_len;
};

BLAKE2bState *glue_blake2b_new(size_t digest_len, const uint8_t personal[16]) {
  BLAKE2bState *st = malloc(sizeof(BLAKE2bState));
  if (!st) { return NULL; }
  st->digest_len = digest_len;

  blake2b_param P;
  memset(&P, 0, sizeof(P));
  P.digest_length = (uint8_t)digest_len;
  P.key_length = 0;
  P.fanout = 1;
  P.depth = 1;
  memcpy(P.personal, personal, 16);

  if (blake2b_init_param(&st->inner, &P) != 0) {
    free(st);
    return NULL;
  }
  return st;
}

BLAKE2bState *glue_blake2b_clone(const BLAKE2bState *state) {
  BLAKE2bState *out = malloc(sizeof(BLAKE2bState));
  if (!out) { return NULL; }
  memcpy(out, state, sizeof(BLAKE2bState));
  return out;
}

void glue_blake2b_free(BLAKE2bState *state) {
  free(state);
}

void glue_blake2b_update(BLAKE2bState *state, const unsigned char *input, size_t input_len) {
  blake2b_update(&state->inner, input, input_len);
}

void glue_blake2b_finalize(BLAKE2bState *state, unsigned char *output, size_t output_len) {
  // equi_digit0 only ever finalizes a short-lived clone, matching
  // src/blake2b.rs's blake2b_finalize (which calls state.finalize() on a
  // clone made via equi_clone, not the shared base state).
  blake2b_final(&state->inner, output, output_len);
}
