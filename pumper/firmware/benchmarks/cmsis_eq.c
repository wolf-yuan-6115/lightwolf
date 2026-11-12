#include "cmsis_eq.h"
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
bool bench_cmsis_process(bench_cmsis_t *s, int32_t const *in, int32_t *out, size_t frames) {
  if (!s->valid || frames > BENCH_CMSIS_MAX_FRAMES)
    return false;
  for (unsigned ch = 0; ch < 2; ++ch)
    for (size_t n = 0; n < frames; ++n)
      s->work[ch][n] = in[n * 2 + ch];
  size_t ramp = s->remaining < frames ? s->remaining : frames;
  for (size_t n = 0; n < ramp; ++n) {
    advance(s);
    for (unsigned ch = 0; ch < 2; ++ch)
      s->work[ch][n] = transition_sample(s, ch, s->work[ch][n]);
  }
  if (frames > ramp)
    for (unsigned ch = 0; ch < 2; ++ch) {
      // Keep order; combine only consecutive stages with the same postShift.
      for (unsigned first = 0; first < 10;) {
        unsigned end = first + 1;
        while (end < 10 && s->shift[end] == s->shift[first])
          ++end;
        arm_biquad_cas_df1_32x64_ins_q31 instance = {
            (uint8_t)(end - first), &s->state[ch][first][0], &s->coeff[first][0], s->shift[first]};
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
