#include "cmsis_eq.h"
#include "eq_dsp.h"
#include "fixed.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define N 16384u
static int32_t input[N], block[388], actual[388], expected[388];
static double ideal[N];
static float f32[N * 2];
static bench_cmsis_t fast, oracle;
bool bench_no_controls(float *l, float *r) {
  (void)l;
  (void)r;
  return false;
}
static double db(double x) { return 20 * log10(fmax(x, 1e-30)); }
static double response(bench_coeff_t c, double w) {
  double nr = c.b0 + c.b1 * cos(w) + c.b2 * cos(2 * w), ni = -c.b1 * sin(w) - c.b2 * sin(2 * w);
  double dr = 1 + c.a1 * cos(w) + c.a2 * cos(2 * w), di = -c.a1 * sin(w) - c.a2 * sin(2 * w);
  return sqrt((nr * nr + ni * ni) / (dr * dr + di * di));
}
int main(int argc, char **argv) {
  bool optimized = argc == 2 && strcmp(argv[1], "--optimized") == 0;
  assert(argc == 1 || optimized);
  unsigned rates[] = {44100, 48000, 88200, 96000, 176400, 192000};
  double levels[] = {-6, -60, -120};
  puts("fixture,rate,signal,dbfs,headroom,range_failure,response_error_db,rms_error_dbfs,"
       "arithmetic_error_dbfs,float_error_dbfs,improvement_db");
  for (unsigned ri = 0; ri < 6; ++ri)
    for (unsigned fi = 0; fi < BENCH_FIXTURES; ++fi) {
      unsigned rate = rates[ri];
      eq_config_t cfg;
      bench_fixture(&cfg, fi);
      bench_coeff_t c[10], qc[10];
      for (unsigned i = 0; i < 10; ++i)
        bench_coefficients(&c[i], &cfg.filters[i], rate);
      for (unsigned sig = 0; sig < 8; ++sig)
        for (unsigned lev = 0; lev < 3; ++lev) {
          bench_ref_stage_t ref[10];
          bench_reference_reset(ref, c, 10);
          eq_init(rate, &cfg);
          uint32_t seed = 0x93a46f21;
          double amplitude = pow(10, levels[lev] / 20);
          for (unsigned n = 0; n < N; ++n) {
            input[n] = (int32_t)round(bench_signal(n, rate, sig, &seed) * amplitude);
            ideal[n] = bench_reference_sample(ref, 10, 0, input[n] / 2147483648.0);
            f32[2 * n] = f32[2 * n + 1] = (float)input[n] / 65536.0f;
          }
          eq_process_interleaved_stereo(f32, N, NULL, false);
          double ferr = 0;
          for (unsigned n = 0; n < N; ++n) {
            double e = f32[2 * n] / 32768.0 - ideal[n];
            ferr += e * e;
          }
          ferr = sqrt(ferr / N);
          bool valid = false;
          unsigned h;
          // At least three headroom bits, matching the device experiment.
          for (h = 3; h <= 30; ++h) {
            if (!bench_cmsis_init(&oracle, c))
              break;
            valid = true;
            for (unsigned pos = 0; pos < N; pos += 194) {
              unsigned count = N - pos < 194 ? N - pos : 194;
              for (unsigned n = 0; n < count; ++n)
                block[2 * n] = block[2 * n + 1] = (int32_t)bench_round_shift(input[pos + n], h);
              if (!bench_cmsis_oracle(&oracle, block, expected, count)) {
                valid = false;
                break;
              }
            }
            if (valid)
              break;
          }
          double rms = 0, arith = 0, resp = 0;
          if (valid) {
            assert(bench_cmsis_init(&fast, c));
            fast.optimized = optimized;
            oracle = fast;
            for (unsigned i = 0; i < 10; ++i) {
              double scale = ldexp(1, (int)fast.shift[i] - 31);
              qc[i] = (bench_coeff_t){fast.coeff[i][0] * scale, fast.coeff[i][1] * scale,
                                      fast.coeff[i][2] * scale, -fast.coeff[i][3] * scale,
                                      -fast.coeff[i][4] * scale};
            }
            for (unsigned k = 0; k <= 1024; ++k) {
              double w = 6.283185307179586 * 20 * pow(1000.0, k / 1024.0) / rate, a = 1, b = 1;
              for (unsigned j = 0; j < 10; ++j) {
                a *= response(c[j], w);
                b *= response(qc[j], w);
              }
              if (a > 1e-4) {
                double e = fabs(db(b) - db(a));
                if (e > resp)
                  resp = e;
              }
            }
            bench_reference_reset(ref, qc, 10);
            for (unsigned pos = 0; pos < N; pos += 194) {
              unsigned count = N - pos < 194 ? N - pos : 194;
              for (unsigned n = 0; n < count; ++n)
                block[2 * n] = block[2 * n + 1] = (int32_t)bench_round_shift(input[pos + n], h);
              assert(bench_cmsis_oracle(&oracle, block, expected, count));
              assert(bench_cmsis_process(&fast, block, actual, count));
              assert(memcmp(actual, expected, count * 2 * sizeof(int32_t)) == 0);
              for (unsigned n = 0; n < count; ++n) {
                double out = ldexp(actual[2 * n], (int)h - 31),
                       q = bench_reference_sample(ref, 10, 0, input[pos + n] / 2147483648.0);
                double e = out - ideal[pos + n], a = out - q;
                rms += e * e;
                arith += a * a;
              }
            }
            rms = sqrt(rms / N);
            arith = sqrt(arith / N);
          }
          printf("%s,%u,%u,%.0f,%u,%u,%.9g,%.5f,%.5f,%.5f,%.5f\n", bench_fixture_name(fi), rate,
                 sig, levels[lev], h > 30 ? 30 : h, !valid, resp, valid ? db(rms) : NAN,
                 valid ? db(arith) : NAN, db(ferr), valid ? db(ferr) - db(rms) : NAN);
        }
    }
}
