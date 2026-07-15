// Cross-check driver for the vendored, single-core-stripped tromp Equihash
// solver (equihash-0.3.0/tromp/equi_miner.c). This file is NOT part of the
// vendored/pinned source -- it is harness code written for this port, and
// equi_miner.c / equi.h are compiled here completely unmodified (see
// ../README.md for the exact paths).
//
// This binary reproduces exactly what the equihash crate's own src/tromp.rs
// `worker()` function does (hardcoded id=0, no thread spawn, no C worker()
// call): equi_setstate -> equi_digit0(eq,0) -> equi_clearslots ->
// { equi_digitodd|digiteven(eq,r,0) -> equi_clearslots } for r=1..WK ->
// equi_digitK(eq,0). WN/WK/RESTBITS are supplied via -D at compile time,
// one binary per (WN,RESTBITS) pair -- see ../build.rs.
//
// The base BLAKE2b state (personalization "ZcashPoW"+WN_le+WK_le, digest
// length HASHOUT, input+nonce absorbed before equi_setstate) mirrors
// equihash-0.3.0/src/verify.rs::initialise_state and
// src/tromp.rs::solve_200_9_uncompressed exactly.
//
// Output: one JSON object per line to stdout, one per solution found for
// the given (input, nonce): {"indices":[...]}. Errors go to stderr.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// blake2b_glue.h declares an opaque BLAKE2bState + free functions with the
// exact names equi.h's function-pointer typedefs need; its implementation
// (blake2b_glue.c) is a separate translation unit so the reference BLAKE2b
// header never collides with equi_miner.c's own vendored blake2b.h.
#include "blake2b_glue.h"

#include "equi_miner.c" // the vendored, unmodified solver

// ---- adapt glue_blake2b_* to the exact function-pointer signatures
// ---- equi.h's blake2b_clone/free/update/finalize typedefs require ----

static BLAKE2bState *harness_blake2b_clone(const BLAKE2bState *state) {
  return glue_blake2b_clone(state);
}

static void harness_blake2b_free(BLAKE2bState *state) {
  glue_blake2b_free(state);
}

static void harness_blake2b_update(BLAKE2bState *state, const uchar *input, size_t input_len) {
  glue_blake2b_update(state, input, input_len);
}

static void harness_blake2b_finalize(BLAKE2bState *state, uchar *output, size_t output_len) {
  glue_blake2b_finalize(state, output, output_len);
}

static BLAKE2bState *make_base_state(const uint8_t *input, size_t input_len,
                                      const uint8_t *nonce, size_t nonce_len) {
  uint8_t personal[16];
  memset(personal, 0, sizeof(personal));
  memcpy(personal, "ZcashPoW", 8);
  uint32_t wn_le = (uint32_t)WN;
  uint32_t wk_le = (uint32_t)WK;
  // little-endian encode, matching Rust's u32::to_le_bytes()
  for (int i = 0; i < 4; i++) personal[8 + i] = (uint8_t)((wn_le >> (8 * i)) & 0xff);
  for (int i = 0; i < 4; i++) personal[12 + i] = (uint8_t)((wk_le >> (8 * i)) & 0xff);

  BLAKE2bState *state = glue_blake2b_new(HASHOUT, personal);
  if (!state) { fprintf(stderr, "blake2b init failed\n"); exit(1); }
  harness_blake2b_update(state, input, input_len);
  harness_blake2b_update(state, nonce, nonce_len);
  return state;
}

static int hex_decode(const char *hex, uint8_t **out, size_t *out_len) {
  size_t len = strlen(hex);
  if (len % 2 != 0) return -1;
  size_t n = len / 2;
  uint8_t *buf = malloc(n ? n : 1);
  for (size_t i = 0; i < n; i++) {
    unsigned int byte;
    if (sscanf(hex + 2 * i, "%2x", &byte) != 1) { free(buf); return -1; }
    buf[i] = (uint8_t)byte;
  }
  *out = buf;
  *out_len = n;
  return 0;
}

int main(int argc, char **argv) {
  if (argc != 3) {
    fprintf(stderr, "usage: %s <input_hex> <nonce_hex>\n", argv[0]);
    return 2;
  }

  uint8_t *input, *nonce;
  size_t input_len, nonce_len;
  if (hex_decode(argv[1], &input, &input_len) != 0) {
    fprintf(stderr, "bad input hex\n");
    return 2;
  }
  if (hex_decode(argv[2], &nonce, &nonce_len) != 0) {
    fprintf(stderr, "bad nonce hex\n");
    return 2;
  }

  BLAKE2bState *base_state = make_base_state(input, input_len, nonce, nonce_len);

  equi *eq = equi_new(
    harness_blake2b_clone,
    harness_blake2b_free,
    harness_blake2b_update,
    harness_blake2b_finalize
  );

  // Mirrors src/tromp.rs `worker()` exactly: hardcoded id=0, sequential
  // digit rounds, no thread spawn, no call into the C's own worker().
  equi_setstate(eq, base_state);
  equi_digit0(eq, 0);
  equi_clearslots(eq);
  for (u32 r = 1; r < WK; r++) {
    if (r & 1) {
      equi_digitodd(eq, r, 0);
    } else {
      equi_digiteven(eq, r, 0);
    }
    equi_clearslots(eq);
  }
  equi_digitK(eq, 0);

  size_t nsols = equi_nsols(eq);
  const proof *sols = (const proof *)equi_sols(eq);
  const u32 solution_len = 1u << WK;

  for (size_t i = 0; i < nsols; i++) {
    printf("{\"indices\":[");
    for (u32 j = 0; j < solution_len; j++) {
      printf("%u%s", sols[i][j], j + 1 < solution_len ? "," : "");
    }
    printf("]}\n");
  }

  equi_free(eq);
  harness_blake2b_free(base_state);
  free(input);
  free(nonce);
  return 0;
}
