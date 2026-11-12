#include "eq_dsp.h"
#include "fixed.h"
#include <math.h>
#include <stdio.h>
bool bench_no_controls(float *l, float *r) {
  (void)l;
  (void)r;
  return false;
}
int main(void) {
  printf("fixture,rate,variant,tail_rms_dbfs,tail_peak_dbfs,overflow,clip\n");
  unsigned rates[] = {96000, 192000}, fixtures[] = {0, 2};
  for (unsigned ri = 0; ri < 2; ++ri)
    for (unsigned fi = 0; fi < 2; ++fi) {
      unsigned rate = rates[ri];
      eq_config_t cfg;
      bench_fixture(&cfg, fixtures[fi]);
      bench_coeff_t c[10];
      for (unsigned i = 0; i < 10; ++i)
        bench_coefficients(&c[i], &cfg.filters[i], rate);
      bench_fixed_t fixed[2];
      bench_fixed_init(&fixed[0], c, false);
      bench_fixed_init(&fixed[1], c, true);
      eq_init(rate, &cfg);
      double sum[3] = {0}, peak[3] = {0};
      for (unsigned n = 0; n < rate * 10; ++n) {
        int32_t in = n == 0 ? 1073741824 : 0;
        float pair[2] = {(float)in / 65536.0f, 0};
        eq_process_interleaved_stereo(pair, 1, NULL, false);
        double out[3] = {pair[0] / 32768.0, bench_fixed_sample(&fixed[0], 0, in) / 2147483648.0,
                         bench_fixed_sample(&fixed[1], 0, in) / 2147483648.0};
        if (n >= rate * 9)
          for (unsigned i = 0; i < 3; ++i) {
            sum[i] += out[i] * out[i];
            if (fabs(out[i]) > peak[i])
              peak[i] = fabs(out[i]);
          }
      }
      char const *names[] = {"float", "q31-state32", "q31-state64"};
      for (unsigned i = 0; i < 3; ++i)
        printf("%s,%u,%s,%.5f,%.5f,%u,%u\n", bench_fixture_name(fixtures[fi]), rate, names[i],
               20 * log10(fmax(sqrt(sum[i] / rate), 1e-30)), 20 * log10(fmax(peak[i], 1e-30)),
               i ? fixed[i - 1].overflows : 0, i ? fixed[i - 1].clips : 0);
    }
}
