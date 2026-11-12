#ifndef BENCH_FIXED_H
#define BENCH_FIXED_H
#include "reference.h"
typedef struct {
  int32_t c[5], target[5];
  int64_t step[5], remainder[5], error[5];
  uint32_t remaining, duration;
  unsigned shift;
  int32_t x[2][2];
  int64_t y[2][2];
  bool identity;
} bench_fixed_stage_t;
typedef struct {
  bench_fixed_stage_t stage[EQ_NUM_FILTERS];
  bool wide_state;
  uint32_t overflows, clips, unstable;
} bench_fixed_t;
int64_t bench_round_shift(int64_t x, unsigned shift);
int64_t bench_mul32x64(int32_t a, int64_t b);
bool bench_fixed_init(bench_fixed_t *s, bench_coeff_t const *c, bool wide);
bool bench_fixed_ramp(bench_fixed_t *s, bench_coeff_t const *target, uint32_t frames);
int32_t bench_fixed_sample(bench_fixed_t *s, unsigned channel, int32_t x);
void bench_fixed_coefficients(bench_fixed_t const *s, bench_coeff_t *c);
#endif
