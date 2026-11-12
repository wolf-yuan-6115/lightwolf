#include "reference.h"
#include <assert.h>
#include <math.h>
int main(void) {
  uint32_t rates[] = {44100, 48000, 88200, 96000, 176400, 192000};
  for (unsigned r = 0; r < 6; ++r)
    for (unsigned f = 0; f < BENCH_FIXTURES; ++f) {
      eq_config_t cfg;
      bench_fixture(&cfg, f);
      assert(eq_config_validate(&cfg));
      bench_coeff_t c[10];
      bench_ref_stage_t s[10];
      for (unsigned i = 0; i < 10; ++i)
        assert(bench_coefficients(&c[i], &cfg.filters[i], rates[r]));
      bench_reference_reset(s, c, 10);
      double peak = 0;
      for (unsigned n = 0; n < 10000; ++n) {
        double v = bench_reference_sample(s, 10, 0, n == 0 ? 1.0 : 0.0);
        assert(isfinite(v));
        if (f == 1)
          assert(v == (n == 0 ? 1 : 0));
        if (fabs(v) > peak)
          peak = fabs(v);
      }
      assert(isfinite(peak));
    }
  eq_config_t cfg;
  bench_fixture(&cfg, 2);
  bench_coeff_t c;
  assert(bench_coefficients(&c, &cfg.filters[0], 96000));
  double w = 6.283185307179586 * 20 / 96000;
  double nr = c.b0 + c.b1 * cos(w) + c.b2 * cos(2 * w), ni = -c.b1 * sin(w) - c.b2 * sin(2 * w);
  double dr = 1 + c.a1 * cos(w) + c.a2 * cos(2 * w), di = -c.a1 * sin(w) - c.a2 * sin(2 * w);
  assert(fabs(10 * log10((nr * nr + ni * ni) / (dr * dr + di * di)) - 12) < 1e-6);
  // Configuration boundary matrix: every type/rate, both gain extremes,
  // frequency extremes, Q/shelf limits and bandwidth limits.
  for (unsigned r = 0; r < 6; ++r)
    for (unsigned type = 0; type <= EQ_FILTER_BAND_PASS; ++type)
      for (unsigned edge = 0; edge < 16; ++edge) {
        bench_fixture(&cfg, 1);
        bool shelf = type == EQ_FILTER_LOW_SHELF || type == EQ_FILTER_HIGH_SHELF;
        eq_filter_config_t f = {true, (eq_filter_type_t)type,
                               type == EQ_FILTER_PEAKING ? EQ_WIDTH_BANDWIDTH : EQ_WIDTH_Q,
                               edge & 1 ? 20000 : 20,
                               edge & 2 ? (shelf ? 1 : 20) : 0.1f,
                               edge & 4 ? 4 : 0.1f, type < 3 ? (edge & 8 ? 24 : -24) : 0};
        cfg.filters[0] = f;
        cfg.preamp_db = edge & 8 ? EQ_PREAMP_MAX_DB : EQ_PREAMP_MIN_DB;
        assert(eq_config_validate(&cfg));
        assert(bench_coefficients(&c, &f, rates[r]));
        assert(isfinite(c.b0) && isfinite(c.b1) && isfinite(c.b2));
        assert(isfinite(c.a1) && isfinite(c.a2));
      }
  // A fixed Q31 scale cannot represent full-scale audio after the minimum
  // existing preamp gain. Keep this setting in the future fallback envelope.
  assert(round(pow(10.0, EQ_PREAMP_MIN_DB / 20.0) * 2147483647.0) == 0);
}
