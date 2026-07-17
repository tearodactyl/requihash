/* uniblake CPU probe interface. */
#ifndef UB_PROBE_H
#define UB_PROBE_H

typedef struct {
  int neon;   /* AArch64 ASIMD (base ISA on aarch64) */
  int sse41;  /* x86 SSE4.1 */
  int avx2;   /* x86 AVX2 */
} ub_cpu_features;

ub_cpu_features ub_detect_cpu(void);

#endif /* UB_PROBE_H */
