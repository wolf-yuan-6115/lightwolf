#include "cmsis_eq.h"
#include "m33_mac.h"
#include <limits.h>
#include <math.h>
#include <string.h>
static bool coefficients(bench_cmsis_t *s, bench_coeff_t const *c, bool ramp) {
  for (unsigned i = 0; i < 10; ++i) {
    double v[5] = {c[i].b0, c[i].b1, c[i].b2, -c[i].a1, -c[i].a2}, peak = 0;
    for (unsigned k = 0; k < 5; ++k)
      if (fabs(v[k]) > peak)
        peak = fabs(v[k]);
    unsigned shift = 0;
    while (shift < 30 && peak >= ldexp(1.0, shift))
      ++shift;
    if (ramp && shift < s->shift[i])
      shift = s->shift[i];
    for (unsigned k = 0; k < 5; ++k) {
      if (ramp && shift != s->shift[i])
        s->coeff[i][k] = (int32_t)round(ldexp(s->coeff[i][k], (int)s->shift[i] - (int)shift));
      double x = round(ldexp(v[k], 31 - (int)shift));
      if (!isfinite(x) || x > INT32_MAX || x < INT32_MIN)
        return false;
      s->target[i][k] = (int32_t)x;
      if (!ramp)
        s->coeff[i][k] = (int32_t)x;
    }
    s->shift[i] = (uint8_t)shift;
    double factor = ldexp(1.0, (int)shift - 31);
    double a1 = -s->target[i][3] * factor, a2 = -s->target[i][4] * factor;
    if (!(fabs(a2) < 1 && 1 + a1 + a2 > 0 && 1 - a1 + a2 > 0))
      return false;
  }
  return true;
}
bool bench_cmsis_init(bench_cmsis_t *s, bench_coeff_t const *c) {
  memset(s, 0, sizeof(*s));
  s->valid = coefficients(s, c, false);
  return s->valid;
}
bool bench_cmsis_ramp(bench_cmsis_t *s, bench_coeff_t const *c, uint32_t frames) {
  if (!s->valid || frames < 2 || frames > 192000)
    return false;
  s->valid = coefficients(s, c, true);
  if (!s->valid)
    return false;
  s->remaining = s->duration = frames;
  for (unsigned i = 0; i < 10; ++i)
    for (unsigned k = 0; k < 5; ++k) {
      int64_t delta = (int64_t)s->target[i][k] - s->coeff[i][k];
      s->step[i][k] = (int32_t)(delta / frames);
      s->remainder[i][k] = (int32_t)(delta % frames);
      s->error[i][k] = 0;
    }
  return true;
}
static inline void advance(bench_cmsis_t *s) {
  if (!s->remaining)
    return;
  for (unsigned i = 0; i < 10; ++i)
    for (unsigned k = 0; k < 5; ++k) {
      s->coeff[i][k] += s->step[i][k];
      s->error[i][k] += s->remainder[i][k];
      if (s->error[i][k] >= (int32_t)s->duration) {
        ++s->coeff[i][k];
        s->error[i][k] -= s->duration;
      }
      if (s->error[i][k] <= -(int32_t)s->duration) {
        --s->coeff[i][k];
        s->error[i][k] += s->duration;
      }
    }
  if (!--s->remaining)
    memcpy(s->coeff, s->target, sizeof(s->coeff));
}
static inline int64_t multiply(int64_t x, int32_t y) {
  // Same arithmetic floor as CMSIS mult32x64; no rounding-to-nearest here.
  return (((int64_t)(uint32_t)x * y) >> 32) + ((x >> 32) * y);
}
static inline int32_t transition_sample(bench_cmsis_t *s, unsigned ch, int32_t x) {
  for (unsigned i = 0; i < 10; ++i) {
    int32_t const *c = s->coeff[i];
    int64_t *h = s->state[ch][i];
    int64_t acc = (int64_t)c[0] * x + (int64_t)c[1] * (int32_t)h[0] + (int64_t)c[2] * (int32_t)h[1];
    acc += multiply(h[2], c[3]);
    acc += multiply(h[3], c[4]);
    uint64_t bits = (uint64_t)acc << (s->shift[i] + 1);
    h[1] = h[0];
    h[0] = x;
    h[3] = h[2];
    memcpy(&h[2], &bits, sizeof(bits));
    x = (int32_t)(h[2] >> 32);
  }
  return x;
}
static inline int32_t optimized_sample(int32_t x, int32_t const *c, int64_t *h, unsigned shift) {
  int64_t acc = (int64_t)c[0] * x + (int64_t)c[1] * (int32_t)h[0] + (int64_t)c[2] * (int32_t)h[1];
  acc = bench_m33_mac(acc, h[2], c[3]);
  acc = bench_m33_mac(acc, h[3], c[4]);
  uint64_t bits = (uint64_t)acc << (shift + 1);
  h[1] = h[0];
  h[0] = x;
  h[3] = h[2];
  memcpy(&h[2], &bits, sizeof(bits));
  return (int32_t)(h[2] >> 32);
}
static void optimized_stage(int32_t *samples, size_t frames, int32_t const *c, int64_t *h,
                            unsigned shift) {
  int32_t x1 = (int32_t)h[0], x2 = (int32_t)h[1];
  int64_t y1 = h[2], y2 = h[3];
  int32_t b0 = c[0], b1 = c[1], b2 = c[2], a1 = c[3], a2 = c[4];
  for (size_t n = 0; n < frames; ++n) {
    int32_t x = samples[n];
    int64_t acc = (int64_t)b0 * x + (int64_t)b1 * x1 + (int64_t)b2 * x2;
    acc = bench_m33_mac(acc, y1, a1);
    acc = bench_m33_mac(acc, y2, a2);
    uint64_t bits = (uint64_t)acc << (shift + 1);
    x2 = x1;
    x1 = x;
    y2 = y1;
    memcpy(&y1, &bits, sizeof(bits));
    samples[n] = (int32_t)(y1 >> 32);
  }
  h[0] = x1;
  h[1] = x2;
  h[2] = y1;
  h[3] = y2;
}
static void advance_stage(bench_cmsis_t *s, unsigned i) {
  for (unsigned k = 0; k < 5; ++k) {
    s->coeff[i][k] += s->step[i][k];
    s->error[i][k] += s->remainder[i][k];
    if (s->error[i][k] >= (int32_t)s->duration) {
      ++s->coeff[i][k];
      s->error[i][k] -= (int32_t)s->duration;
    }
    if (s->error[i][k] <= -(int32_t)s->duration) {
      --s->coeff[i][k];
      s->error[i][k] += (int32_t)s->duration;
    }
  }
}
bool bench_cmsis_process(bench_cmsis_t *s, int32_t const *in, int32_t *out, size_t frames) {
  if (!s->valid || frames > BENCH_CMSIS_MAX_FRAMES)
    return false;
  for (unsigned ch = 0; ch < 2; ++ch)
    for (size_t n = 0; n < frames; ++n)
      s->work[ch][n] = in[n * 2 + ch];
  size_t ramp = s->remaining < frames ? s->remaining : frames;
  if (s->optimized) {
    // Process one stage's coefficient trajectory across a block. This is
    // causally identical to sample-major processing but reuses stage state.
    for (unsigned i = 0; i < 10; ++i)
      for (size_t n = 0; n < ramp; ++n) {
        advance_stage(s, i);
        for (unsigned ch = 0; ch < 2; ++ch)
          s->work[ch][n] =
              optimized_sample(s->work[ch][n], s->coeff[i], s->state[ch][i], s->shift[i]);
      }
    s->remaining -= (uint32_t)ramp;
    if (!s->remaining)
      memcpy(s->coeff, s->target, sizeof(s->coeff));
  } else
    for (size_t n = 0; n < ramp; ++n) {
      advance(s);
      for (unsigned ch = 0; ch < 2; ++ch)
        s->work[ch][n] = transition_sample(s, ch, s->work[ch][n]);
    }
  if (frames > ramp)
    for (unsigned ch = 0; ch < 2; ++ch) {
      if (s->optimized) {
        for (unsigned i = 0; i < 10; ++i)
          optimized_stage(s->work[ch] + ramp, frames - ramp, s->coeff[i], s->state[ch][i],
                          s->shift[i]);
      } else
        for (unsigned first = 0; first < 10;) {
          unsigned end = first + 1;
          while (end < 10 && s->shift[end] == s->shift[first])
            ++end;
          arm_biquad_cas_df1_32x64_ins_q31 instance = {(uint8_t)(end - first),
                                                       &s->state[ch][first][0], &s->coeff[first][0],
                                                       s->shift[first]};
          arm_biquad_cas_df1_32x64_q31(&instance, s->work[ch] + ramp, s->work[ch] + ramp,
                                       (uint32_t)(frames - ramp));
          first = end;
        }
    }
  for (size_t n = 0; n < frames; ++n)
    for (unsigned ch = 0; ch < 2; ++ch)
      out[n * 2 + ch] = s->work[ch][n];
  return true;
}
bool bench_cmsis_selftest(void) {
  // Static buffers avoid consuming either core's 2 KiB stack. Called before USB
  // enumeration so validation cannot delay an active audio stream.
  static bench_cmsis_t a, b;
  static int32_t in[388], x[388], y[388];
  static bench_coeff_t c[10];
  eq_config_t cfg;
  bench_fixture(&cfg, 0);
  for (unsigned i = 0; i < 10; ++i)
    bench_coefficients(&c[i], &cfg.filters[i], 96000);
  if (!bench_cmsis_init(&a, c) || !bench_cmsis_init(&b, c))
    return false;
  b.optimized = true;
  uint32_t seed = 0x93a46f21;
  unsigned sizes[] = {1, 3, 4, 7, 96, 97, 194};
  for (unsigned n = 0; n < 80; ++n) {
    unsigned count = sizes[n % 7];
    for (unsigned i = 0; i < count * 2; ++i) {
      seed = seed * 1664525 + 1013904223;
      in[i] = ((int32_t)(seed & INT32_MAX) - 1073741824) / 8;
    }
    if (n == 5 || n == 8) {
      for (unsigned i = 0; i < 10; ++i) {
        cfg.filters[i].frequency_hz *= 1.01f;
        bench_coefficients(&c[i], &cfg.filters[i], 96000);
      }
      if (!bench_cmsis_ramp(&a, c, 960) || !bench_cmsis_ramp(&b, c, 960))
        return false;
    }
    if (!bench_cmsis_process(&a, in, x, count) || !bench_cmsis_process(&b, in, y, count))
      return false;
    if (memcmp(x, y, count * 2 * sizeof(int32_t)) || memcmp(a.state, b.state, sizeof(a.state)))
      return false;
  }
  return true;
}
#ifdef BENCH_CMSIS_ORACLE
static __int128 floor_shift(__int128 x, unsigned bits) {
  __int128 scale = (__int128)1 << bits;
  return x >= 0 ? x / scale : -((-x + scale - 1) / scale);
}
bool bench_cmsis_oracle(bench_cmsis_t *s, int32_t const *in, int32_t *out, size_t frames) {
  if (!s->valid)
    return false;
  for (size_t n = 0; n < frames; ++n) {
    advance(s);
    for (unsigned ch = 0; ch < 2; ++ch) {
      int32_t x = in[n * 2 + ch];
      for (unsigned i = 0; i < 10; ++i) {
        int32_t const *c = s->coeff[i];
        int64_t *h = s->state[ch][i];
        __int128 acc = (__int128)c[0] * x;
        __int128 terms[4] = {(__int128)c[1] * h[0], (__int128)c[2] * h[1],
                             floor_shift((__int128)c[3] * h[2], 32),
                             floor_shift((__int128)c[4] * h[3], 32)};
        for (unsigned j = 0; j < 4; ++j) {
          acc += terms[j];
          if (acc < INT64_MIN || acc > INT64_MAX)
            return false;
        }
        __int128 state = acc * ((__int128)1 << (s->shift[i] + 1));
        if (state < INT64_MIN || state > INT64_MAX)
          return false;
        h[1] = h[0];
        h[0] = x;
        h[3] = h[2];
        h[2] = (int64_t)state;
        x = (int32_t)floor_shift(state, 32);
      }
      out[n * 2 + ch] = x;
    }
  }
  return true;
}
#endif
