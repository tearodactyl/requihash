/* uniblake oracle backend interface.
 *
 * The oracle is the pristine vendored reference (../../vendor/blake2/
 * blake2b-ref.c), compiled UNTOUCHED in its own translation unit
 * (ub_oracle.c wraps it with a symbol prefix so its public functions
 * don't collide with the uniblake core). It is the §1a conformance
 * anchor: every kernel is validated byte-for-byte against it. It is
 * never dispatched to for real hashing — it exists only to gate the
 * kernels. Provenance: ../PROVENANCE.md.
 */
#ifndef UB_ORACLE_H
#define UB_ORACLE_H

#include <stddef.h>
#include <stdint.h>

/* One-shot BLAKE2b through the untouched reference, with optional
 * 16-byte personalization (NULL = none). Returns 0 on success. */
int ub_oracle_blake2b(void *out, size_t outlen, const void *in, size_t inlen,
                      const uint8_t personal[16]);

#endif /* UB_ORACLE_H */
