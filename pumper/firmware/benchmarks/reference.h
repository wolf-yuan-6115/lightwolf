#ifndef BENCH_REFERENCE_H
#define BENCH_REFERENCE_H
#include "eq_config.h"
#include <stddef.h>
#include <stdint.h>
typedef struct { double b0, b1, b2, a1, a2; } bench_coeff_t;
typedef struct { bench_coeff_t c; double x[2][2], y[2][2]; } bench_ref_stage_t;
bool bench_coefficients(bench_coeff_t *out, eq_filter_config_t const *cfg, double fs);
void bench_reference_reset(bench_ref_stage_t *s, bench_coeff_t const *c, size_t stages);
double bench_reference_sample(bench_ref_stage_t *s, size_t stages, unsigned channel, double x);
void bench_fixture(eq_config_t *cfg, unsigned fixture);
#define BENCH_FIXTURES 7u
char const *bench_fixture_name(unsigned fixture);
int32_t bench_signal(uint32_t frame, uint32_t rate, unsigned signal, uint32_t *seed);
#endif
