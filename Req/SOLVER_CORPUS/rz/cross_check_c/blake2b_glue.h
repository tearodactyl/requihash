// Opaque BLAKE2b glue between the vendored equi_miner.c (which only ever
// sees a forward-declared `struct BLAKE2bState` plus 4 function pointers,
// per equihash-0.3.0/tromp/blake2b.h) and the reference BLAKE2b
// implementation at ~/Work/ZK/ZKs/blake2-reference/ref (Samuel Neves, CC0).
//
// This header is included by harness_main.c BEFORE equi_miner.c, and
// defines the exact `BLAKE2bState` name and 4 function-pointer-compatible
// symbol signatures equi.h/equi_miner.c expect -- but implemented in
// blake2b_glue.c, which is the only translation unit that also includes the
// reference blake2.h, so there is never a name collision between the two
// blake2b header worlds in a single translation unit.

#ifndef RZ_BLAKE2B_GLUE_H
#define RZ_BLAKE2B_GLUE_H

#include <stddef.h>
#include <stdint.h>

typedef struct BLAKE2bState BLAKE2bState;

BLAKE2bState *glue_blake2b_new(size_t digest_len, const uint8_t personal[16]);
BLAKE2bState *glue_blake2b_clone(const BLAKE2bState *state);
void glue_blake2b_free(BLAKE2bState *state);
void glue_blake2b_update(BLAKE2bState *state, const unsigned char *input, size_t input_len);
void glue_blake2b_finalize(BLAKE2bState *state, unsigned char *output, size_t output_len);

#endif // RZ_BLAKE2B_GLUE_H
