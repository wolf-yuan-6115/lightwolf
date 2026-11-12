#include "cmsis_eq.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
static bench_cmsis_t fast, oracle;
static int32_t input[388], actual[388], expected[388];
static void check(size_t frames) {
  assert(bench_cmsis_oracle(&oracle, input, expected, frames));
  assert(bench_cmsis_process(&fast, input, actual, frames));
  assert(memcmp(actual, expected, frames * 2 * sizeof(int32_t)) == 0);
  assert(memcmp(fast.state, oracle.state, sizeof(fast.state)) == 0);
}
int main(void) {
  assert(bench_cmsis_selftest());
  bench_coeff_t c[10];
  for (unsigned i = 0; i < 10; ++i)
    c[i] = (bench_coeff_t){1, 0, 0, 0, 0};
  assert(bench_cmsis_init(&fast, c));
  oracle = fast;
  int32_t edges[] = {INT32_MIN, INT32_MAX, -1, 0, 1, 16777217, 16777218};
  for (unsigned n = 0; n < 7; ++n) {
    input[n * 2] = edges[n];
    input[n * 2 + 1] = edges[6 - n];
  }
  check(7);
  assert(memcmp(actual, input, 14 * sizeof(int32_t)) == 0);
  uint32_t seed = 0x93a46f21;
  unsigned rates[] = {44100, 48000, 88200, 96000, 176400, 192000};
  size_t sizes[] = {1, 3, 4, 7, 96, 97, 194};
  for (unsigned opt = 0; opt < 2; ++opt)
    for (unsigned r = 0; r < 6; ++r) {
      eq_config_t cfg;
      bench_fixture(&cfg, 0);
      for (unsigned i = 0; i < 10; ++i)
        bench_coefficients(&c[i], &cfg.filters[i], rates[r]);
      assert(bench_cmsis_init(&fast, c));
      fast.optimized = opt;
      oracle = fast;
      for (unsigned n = 0; n < 300; ++n) {
        size_t frames = sizes[n % 7];
        for (size_t j = 0; j < frames * 2; ++j) {
          seed = seed * 1664525 + 1013904223;
          input[j] = ((int32_t)(seed & 0x7fffffff) - 1073741824) / 8;
        }
        if (n == 10 || n == 13 || n == 100) {
          // Interrupt an active transition and later return to the original.
          for (unsigned i = 0; i < 10; ++i) {
            cfg.filters[i].frequency_hz *= n == 13 ? 0.99f : 1.01f;
            bench_coefficients(&c[i], &cfg.filters[i], rates[r]);
          }
          assert(bench_cmsis_ramp(&fast, c, rates[r] / 100));
          assert(bench_cmsis_ramp(&oracle, c, rates[r] / 100));
        }
        check(frames);
      }
      memset(input, 0, sizeof(input));
      for (unsigned n = 0; n < 1000; ++n)
        check(194);
    }
  // Exercise rescaling coefficients without changing the history format.
  for (unsigned i = 0; i < 10; ++i)
    c[i] = (bench_coeff_t){1, 0, 0, 0, 0};
  assert(bench_cmsis_init(&fast, c));
  oracle = fast;
  c[0].b0 = 8;
  assert(bench_cmsis_ramp(&fast, c, 960));
  assert(bench_cmsis_ramp(&oracle, c, 960));
  for (unsigned i = 0; i < 388; ++i)
    input[i] = (i & 1) ? -1000 : 1000;
  for (unsigned n = 0; n < 10; ++n)
    check(194);
  assert(actual[0] == 8000 && actual[1] == -8000);
  input[0] = INT32_MAX;
  assert(!bench_cmsis_oracle(&oracle, input, expected, 1));
  puts("CMSIS and transition outputs/state match the checked 128-bit oracle");
}
