#include "fixed.h"
#include <limits.h>
#include <math.h>
#include <string.h>
// Original portable implementations of the DF1 equations. No CMSIS source is
// embedded. Q31 feedback retains either 31 or 63 fractional bits of state.
static uint64_t magnitude(int64_t x) { return x < 0 ? 0u - (uint64_t)x : (uint64_t)x; }
static int64_t signed_magnitude(uint64_t x, bool negative) {
  if (!negative)
    return (int64_t)x;
  return x == (UINT64_C(1) << 63) ? INT64_MIN : -(int64_t)x;
}
int64_t bench_round_shift(int64_t x, unsigned n) {
  if (!n)
    return x;
  // n is 1..63; unsigned arithmetic also handles INT64_MIN.
  uint64_t a = magnitude(x), q = a >> n, mask = (UINT64_C(1) << n) - 1;
  if ((a & mask) >= (UINT64_C(1) << (n - 1)))
    ++q;
  return signed_magnitude(q, x < 0);
}
int64_t bench_mul32x64(int32_t a, int64_t b) {
  uint64_t u = a < 0 ? 0u - (uint64_t)(int64_t)a : (uint64_t)a, v = magnitude(b);
  uint64_t lo = u * (v & UINT32_MAX);
  uint64_t hi = u * (v >> 32) + (lo >> 32);
  if ((lo & UINT32_MAX) >= UINT64_C(0x80000000))
    ++hi;
  return signed_magnitude(hi, (a < 0) != (b < 0));
}
static int64_t add(bench_fixed_t *s, int64_t a, int64_t b) {
  int64_t out;
  if (__builtin_add_overflow(a, b, &out)) {
    ++s->overflows;
    return a < 0 ? INT64_MIN : INT64_MAX;
  }
  return out;
}
static int32_t narrow(bench_fixed_t *s, int64_t a) {
  if (a > INT32_MAX) {
    ++s->clips;
    return INT32_MAX;
  }
  if (a < INT32_MIN) {
    ++s->clips;
    return INT32_MIN;
  }
  return (int32_t)a;
}
static bool encode(bench_coeff_t c, unsigned shift, int32_t *out) {
  double v[5] = {c.b0, c.b1, c.b2, -c.a1, -c.a2};
  for (unsigned i = 0; i < 5; ++i) {
    double x = round(ldexp(v[i], 31 - (int)shift));
    if (!isfinite(x) || x > INT32_MAX || x < INT32_MIN)
      return false;
    out[i] = (int32_t)x;
  }
  return true;
}
static unsigned scale(bench_coeff_t c) {
  double v[5] = {c.b0, c.b1, c.b2, c.a1, c.a2}, peak = 0;
  for (unsigned i = 0; i < 5; ++i)
    if (fabs(v[i]) > peak)
      peak = fabs(v[i]);
  unsigned n = 0;
  while (n < 30 && peak >= ldexp(1.0, n))
    ++n;
  return n;
}
static bench_coeff_t decode(bench_fixed_stage_t const *p) {
  double a = ldexp(1.0, (int)p->shift - 31);
  return (bench_coeff_t){p->c[0] * a, p->c[1] * a, p->c[2] * a, -p->c[3] * a, -p->c[4] * a};
}
static bool stable(bench_coeff_t c) {
  return isfinite(c.a1) && isfinite(c.a2) && fabs(c.a2) < 1 && 1 + c.a1 + c.a2 > 0 &&
         1 - c.a1 + c.a2 > 0;
}
bool bench_fixed_init(bench_fixed_t *s, bench_coeff_t const *c, bool wide) {
  memset(s, 0, sizeof(*s));
  s->wide_state = wide;
  for (unsigned i = 0; i < EQ_NUM_FILTERS; ++i) {
    bench_fixed_stage_t *p = &s->stage[i];
    p->shift = scale(c[i]);
    if (!encode(c[i], p->shift, p->c) || !stable(decode(p))) {
      ++s->unstable;
      return false;
    }
    p->identity = c[i].b0 == 1 && c[i].b1 == 0 && c[i].b2 == 0 && c[i].a1 == 0 && c[i].a2 == 0;
  }
  return true;
}
bool bench_fixed_ramp(bench_fixed_t *s, bench_coeff_t const *target, uint32_t frames) {
  if (!frames)
    return false;
  for (unsigned i = 0; i < EQ_NUM_FILTERS; ++i) {
    bench_fixed_stage_t *p = &s->stage[i];
    bench_coeff_t current = decode(p);
    unsigned n = scale(target[i]);
    if (n > p->shift)
      p->shift = n;
    if (!encode(current, p->shift, p->c) || !encode(target[i], p->shift, p->target)) {
      ++s->unstable;
      return false;
    }
    bench_fixed_stage_t end = *p;
    memcpy(end.c, p->target, sizeof(end.c));
    if (!stable(decode(p)) || !stable(decode(&end))) {
      ++s->unstable;
      return false;
    }
    p->remaining = p->duration = frames;
    p->identity = false;
    for (unsigned j = 0; j < 5; ++j) {
      int64_t d = (int64_t)p->target[j] - p->c[j];
      p->step[j] = d / (int64_t)frames;
      p->remainder[j] = d % (int64_t)frames;
      p->error[j] = 0;
    }
  }
  return true;
}
static void advance(bench_fixed_stage_t *p) {
  if (!p->remaining)
    return;
  for (unsigned j = 0; j < 5; ++j) {
    int64_t v = (int64_t)p->c[j] + p->step[j];
    p->error[j] += p->remainder[j];
    if (p->error[j] >= (int64_t)p->duration) {
      ++v;
      p->error[j] -= p->duration;
    }
    if (p->error[j] <= -(int64_t)p->duration) {
      --v;
      p->error[j] += p->duration;
    }
    p->c[j] = (int32_t)v;
  }
  if (!--p->remaining)
    memcpy(p->c, p->target, sizeof(p->c));
}
int32_t bench_fixed_sample(bench_fixed_t *s, unsigned ch, int32_t x) {
  for (unsigned i = 0; i < EQ_NUM_FILTERS; ++i) {
    bench_fixed_stage_t *p = &s->stage[i];
    if (ch == 0)
      advance(p);
    if (p->identity)
      continue;
    int64_t acc = (int64_t)p->c[0] * x;
    acc = add(s, acc, (int64_t)p->c[1] * p->x[ch][0]);
    acc = add(s, acc, (int64_t)p->c[2] * p->x[ch][1]);
    for (unsigned j = 0; j < 2; ++j)
      acc = add(s, acc,
                s->wide_state ? bench_mul32x64(p->c[3 + j], p->y[ch][j])
                              : (int64_t)p->c[3 + j] * p->y[ch][j]);
    int32_t out = narrow(s, bench_round_shift(acc, 31 - p->shift));
    int64_t history = out;
    if (s->wide_state) {
      uint64_t m = magnitude(acc);
      unsigned shift = p->shift + 1;
      uint64_t limit = acc < 0 ? (UINT64_C(1) << 63) : INT64_MAX;
      if (m > (limit >> shift)) {
        ++s->clips;
        history = acc < 0 ? INT64_MIN : INT64_MAX;
      } else
        history = signed_magnitude(m << shift, acc < 0);
    }
    p->x[ch][1] = p->x[ch][0];
    p->x[ch][0] = x;
    p->y[ch][1] = p->y[ch][0];
    p->y[ch][0] = history;
    x = out;
  }
  return x;
}
void bench_fixed_coefficients(bench_fixed_t const *s, bench_coeff_t *c) {
  for (unsigned i = 0; i < EQ_NUM_FILTERS; ++i)
    c[i] = decode(&s->stage[i]);
}
