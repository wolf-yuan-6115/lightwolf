#ifndef BENCH_CMSIS_EQ_H
#define BENCH_CMSIS_EQ_H
#include "dsp/filtering_functions.h"
#include "reference.h"
#define BENCH_CMSIS_MAX_FRAMES 194u
typedef struct {
  q31_t coeff[10][5], target[10][5], step[10][5], remainder[10][5], error[10][5];
  q63_t state[2][10][4];
  uint8_t shift[10];
  uint32_t remaining, duration;
  q31_t work[2][BENCH_CMSIS_MAX_FRAMES];
  bool valid, optimized;
} bench_cmsis_t;
bool bench_cmsis_selftest(void);
bool bench_cmsis_init(bench_cmsis_t *s, bench_coeff_t const *c);
bool bench_cmsis_ramp(bench_cmsis_t *s, bench_coeff_t const *target, uint32_t frames);
// Fast kernels do not check accumulator range. The caller must establish
// safe headroom; stable coefficients alone do not prove this. Experimental only.
bool bench_cmsis_process(bench_cmsis_t *s, int32_t const *in, int32_t *out, size_t frames);
// Host oracle: matches CMSIS truncation but checks exact sums before narrowing.
#ifdef BENCH_CMSIS_ORACLE
bool bench_cmsis_oracle(bench_cmsis_t *s, int32_t const *in, int32_t *out, size_t frames);
#endif
#endif
