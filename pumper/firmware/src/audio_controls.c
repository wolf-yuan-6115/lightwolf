#include "audio_controls.h"
#include "eq_protocol.h"
#include <math.h>
#include <string.h>

#define DELAY_SIZE 128u
#define PI 3.14159265358979323846f
#define FULL_SCALE 32768.0f
#define LIMITER_RECOVERY_GAIN 0.9999f

_Static_assert(DELAY_SIZE > (192000u * 600u / 1000000u) + 1u, "Delay history too small");
crossfeed_config_t const k_crossfeed_default = {CROSSFEED_OFF, 2000, 700, 250};
output_processing_config_t const k_output_processing_default = {0u, 0, 10000u};

bool crossfeed_validate(crossfeed_config_t const *c) {
  return c && c->mode <= CROSSFEED_CUSTOM && c->strength_bp <= 4000 && c->cutoff_hz >= 300 &&
         c->cutoff_hz <= 2000 && c->delay_us <= 600;
}
bool crossfeed_equal(crossfeed_config_t const *a, crossfeed_config_t const *b) {
  return a->mode == b->mode && a->strength_bp == b->strength_bp && a->cutoff_hz == b->cutoff_hz &&
         a->delay_us == b->delay_us;
}
void crossfeed_encode(uint8_t *p, crossfeed_config_t const *c) {
  p[0] = c->mode; p[1] = 0;
  eq_protocol_write_u16(p + 2, c->strength_bp);
  eq_protocol_write_u16(p + 4, c->cutoff_hz);
  eq_protocol_write_u16(p + 6, c->delay_us);
}
bool crossfeed_decode(uint8_t const *p, size_t n, crossfeed_config_t *c) {
  if (!p || !c || n != CROSSFEED_RECORD_SIZE || p[1]) return false;
  crossfeed_config_t v = {p[0], eq_protocol_read_u16(p + 2), eq_protocol_read_u16(p + 4),
                          eq_protocol_read_u16(p + 6)};
  if (!crossfeed_validate(&v)) return false;
  *c = v;
  return true;
}

bool output_processing_validate(output_processing_config_t const *c) {
  return c && !(c->flags & 0xf0u) && c->balance_bp >= -10000 && c->balance_bp <= 10000 &&
         c->width_bp <= 20000u;
}
bool output_processing_equal(output_processing_config_t const *a,
                             output_processing_config_t const *b) {
  return a->flags == b->flags && a->balance_bp == b->balance_bp && a->width_bp == b->width_bp;
}
void output_processing_encode(uint8_t *p, output_processing_config_t const *c) {
  memset(p, 0, OUTPUT_PROCESSING_RECORD_SIZE);
  p[0] = c->flags;
  eq_protocol_write_u16(p + 2, (uint16_t)c->balance_bp);
  eq_protocol_write_u16(p + 4, c->width_bp);
}
bool output_processing_decode(uint8_t const *p, size_t n, output_processing_config_t *c) {
  if (!p || !c || n != OUTPUT_PROCESSING_RECORD_SIZE || p[1] || p[6] || p[7]) return false;
  output_processing_config_t v = {p[0], (int16_t)eq_protocol_read_u16(p + 2),
                                  eq_protocol_read_u16(p + 4)};
  if (!output_processing_validate(&v)) return false;
  *c = v;
  return true;
}

bool audio_volume_valid(int16_t v) { return v >= -12800 && v <= 0 && v % 256 == 0; }
bool audio_host_control_set(uint8_t channel, uint8_t selector, uint8_t const *p, size_t n,
                            int16_t volume[3], int8_t mute[3]) {
  if (channel || !p || !volume || !mute) return false;
  if (selector == 1u && n == 1u && p[0] <= 1u) { mute[0] = (int8_t)p[0]; return true; }
  if (selector == 2u && n == 2u) {
    int16_t v = (int16_t)eq_protocol_read_u16(p);
    if (!audio_volume_valid(v)) return false;
    volume[0] = v; return true;
  }
  return false;
}
void audio_host_controls_encode(uint8_t *p, int16_t const v[3], int8_t const m[3]) {
  for (unsigned i = 0; i < 3; i++) { eq_protocol_write_u16(p + 2 * i, (uint16_t)v[i]); p[6 + i] = (uint8_t)m[i]; }
}
void crossfeed_state_encode(uint8_t *p, crossfeed_config_t const *live,
                            crossfeed_config_t const *saved) {
  crossfeed_encode(p, live); crossfeed_encode(p + 8, saved); p[16] = !crossfeed_equal(live, saved);
}
void output_processing_state_encode(uint8_t *p, output_processing_config_t const *live,
                                    output_processing_config_t const *saved) {
  output_processing_encode(p, live); output_processing_encode(p + 8, saved);
  p[16] = !output_processing_equal(live, saved);
}
void audio_effective_gains(int16_t const v[3], int8_t const m[3], float g[2]) {
  for (unsigned i = 0; i < 2; i++) {
    int db = (int)v[0] + v[i + 1];
    g[i] = (m[0] || m[i + 1]) ? 0.0f : db == 0 ? 1.0f : powf(10.0f, (float)db / 5120.0f);
  }
}

typedef struct {
  float strength, alpha, fraction, normalizer;
  unsigned delay, position;
  float low[2], history[2][DELAY_SIZE];
} crossfeed_processor_t;
typedef struct { float ll, lr, rl, rr; } output_matrix_t;

static crossfeed_processor_t current, next, pending;
static crossfeed_config_t crossfeed_config;
static output_processing_config_t output_config;
static output_matrix_t matrix, matrix_target, matrix_step;
static bool has_pending;
static uint32_t rate_hz, ramp_frames, fade_remaining, gain_remaining, matrix_remaining;
static float gain[2] = {1, 1}, target_gain[2] = {1, 1}, gain_step[2];
static float limiter_gain = 1.0f, limiter_release_alpha;

static void configure_crossfeed(crossfeed_processor_t *s, crossfeed_config_t const *c) {
  static const uint16_t strengths[] = {0, 1000, 2000, 3000};
  static const uint16_t delays[] = {0, 200, 250, 300};
  float strength = (c->mode == CROSSFEED_CUSTOM ? c->strength_bp : strengths[c->mode]) / 10000.0f;
  unsigned cutoff = c->mode == CROSSFEED_CUSTOM ? c->cutoff_hz : 700;
  unsigned us = c->mode == CROSSFEED_CUSTOM ? c->delay_us : delays[c->mode];
  s->strength = strength; s->normalizer = 1.0f / (1.0f + strength);
  s->alpha = 1.0f - expf(-2.0f * PI * (float)cutoff / (float)rate_hz);
  float delay = (float)us * (float)rate_hz / 1000000.0f;
  s->delay = (unsigned)delay; s->fraction = delay - (float)s->delay;
}

static output_matrix_t build_matrix(output_processing_config_t const *c) {
  output_matrix_t m = (c->flags & OUTPUT_PROCESSING_SWAP)
                          ? (output_matrix_t){0.0f, 1.0f, 1.0f, 0.0f}
                          : (output_matrix_t){1.0f, 0.0f, 0.0f, 1.0f};
  if (c->flags & OUTPUT_PROCESSING_INVERT_LEFT) { m.ll = -m.ll; m.lr = -m.lr; }
  if (c->flags & OUTPUT_PROCESSING_INVERT_RIGHT) { m.rl = -m.rl; m.rr = -m.rr; }
  float width = (c->flags & OUTPUT_PROCESSING_MONO) ? 0.0f : (float)c->width_bp / 10000.0f;
  float same = 0.5f * (1.0f + width), opposite = 0.5f * (1.0f - width);
  output_matrix_t widened = {
      same * m.ll + opposite * m.rl, same * m.lr + opposite * m.rr,
      opposite * m.ll + same * m.rl, opposite * m.lr + same * m.rr};
  m = widened;
  if (c->balance_bp > 0) {
    float s = 1.0f - (float)c->balance_bp / 10000.0f; m.ll *= s; m.lr *= s;
  } else if (c->balance_bp < 0) {
    float s = 1.0f + (float)c->balance_bp / 10000.0f; m.rl *= s; m.rr *= s;
  }
  return m;
}

void audio_controls_init(uint32_t rate, crossfeed_config_t const *crossfeed,
                         output_processing_config_t const *output) {
  crossfeed_config = *crossfeed; output_config = *output;
  gain[0] = gain[1] = target_gain[0] = target_gain[1] = 1.0f;
  audio_controls_reset(rate);
}
void audio_controls_reset(uint32_t rate) {
  rate_hz = rate ? rate : 48000u; ramp_frames = rate_hz / 100u;
  if (!ramp_frames) ramp_frames = 1u;
  memset(&current, 0, sizeof(current)); configure_crossfeed(&current, &crossfeed_config);
  next = current; fade_remaining = 0; has_pending = false;
  gain[0] = target_gain[0]; gain[1] = target_gain[1]; gain_remaining = 0;
  matrix = matrix_target = build_matrix(&output_config); matrix_step = (output_matrix_t){0};
  matrix_remaining = 0; limiter_gain = 1.0f;
  limiter_release_alpha = expf(-1.0f / ((float)rate_hz * 0.050f));
}
void audio_controls_set_crossfeed(crossfeed_config_t const *c) {
  if (!crossfeed_validate(c) || crossfeed_equal(&crossfeed_config, c)) return;
  crossfeed_config = *c;
  if (fade_remaining) { pending = current; configure_crossfeed(&pending, c); has_pending = true; }
  else { next = current; configure_crossfeed(&next, c); fade_remaining = ramp_frames; }
}
void audio_controls_set_output_processing(output_processing_config_t const *c) {
  if (!output_processing_validate(c) || output_processing_equal(&output_config, c)) return;
  output_config = *c; matrix_target = build_matrix(c); float s = 1.0f / (float)ramp_frames;
  matrix_step = (output_matrix_t){(matrix_target.ll - matrix.ll) * s,
                                  (matrix_target.lr - matrix.lr) * s,
                                  (matrix_target.rl - matrix.rl) * s,
                                  (matrix_target.rr - matrix.rr) * s};
  matrix_remaining = ramp_frames;
}
void audio_controls_set_gains(float const g[2]) {
  if (g[0] == target_gain[0] && g[1] == target_gain[1]) return;
  for (unsigned i = 0; i < 2; i++) { target_gain[i] = g[i]; gain_step[i] = (g[i] - gain[i]) / (float)ramp_frames; }
  gain_remaining = ramp_frames;
}
static void process_crossfeed(crossfeed_processor_t *s, float l, float r, float *ol, float *or) {
  float input[2] = {l, r}, delayed[2];
  for (unsigned i = 0; i < 2; i++) {
    s->low[i] += s->alpha * (input[i] - s->low[i]); s->history[i][s->position] = s->low[i];
    unsigned a = (s->position + DELAY_SIZE - s->delay) & (DELAY_SIZE - 1u);
    unsigned b = (a + DELAY_SIZE - 1u) & (DELAY_SIZE - 1u);
    delayed[i] = s->history[i][a] + s->fraction * (s->history[i][b] - s->history[i][a]);
  }
  s->position = (s->position + 1u) & (DELAY_SIZE - 1u);
  if (s->strength == 0.0f) { *ol = l; *or = r; return; }
  *ol = (l + s->strength * delayed[1]) * s->normalizer;
  *or = (r + s->strength * delayed[0]) * s->normalizer;
}
static void advance_matrix(void) {
  if (!matrix_remaining) return;
  uint32_t r = --matrix_remaining;
  matrix.ll = matrix_target.ll - matrix_step.ll * (float)r;
  matrix.lr = matrix_target.lr - matrix_step.lr * (float)r;
  matrix.rl = matrix_target.rl - matrix_step.rl * (float)r;
  matrix.rr = matrix_target.rr - matrix_step.rr * (float)r;
  if (!r) matrix = matrix_target;
}
static float limiter_target(float peak) {
  if (!isfinite(peak)) return 0.0f;
  return peak <= FULL_SCALE ? 1.0f : FULL_SCALE / peak;
}
bool audio_controls_process(float *left, float *right) {
  float a = *left, b = *right;
  if (current.strength != 0.0f || fade_remaining) process_crossfeed(&current, a, b, &a, &b);
  if (fade_remaining) {
    float x, y; process_crossfeed(&next, *left, *right, &x, &y);
    float blend = (float)(ramp_frames - fade_remaining + 1u) / (float)ramp_frames;
    a += blend * (x - a); b += blend * (y - b);
    if (!--fade_remaining) {
      current = next;
      if (has_pending) {
        next = current; next.strength = pending.strength; next.normalizer = pending.normalizer;
        next.alpha = pending.alpha; next.delay = pending.delay; next.fraction = pending.fraction;
        has_pending = false; fade_remaining = ramp_frames;
      }
    }
  }
  if (gain_remaining) {
    --gain_remaining;
    for (unsigned i = 0; i < 2; i++) gain[i] = target_gain[i] - gain_step[i] * (float)gain_remaining;
  }
  a *= gain[0]; b *= gain[1]; advance_matrix();
  float ol = matrix.ll * a + matrix.lr * b;
  float or = matrix.rl * a + matrix.rr * b;
  float target = limiter_target(fmaxf(fabsf(ol), fabsf(or)));
  limiter_gain = target < limiter_gain ? target : target + (limiter_gain - target) * limiter_release_alpha;
  if (target == 1.0f && limiter_gain > LIMITER_RECOVERY_GAIN) limiter_gain = 1.0f;
  bool limiter_active = limiter_gain < 1.0f;
  float limited_l = ol * limiter_gain, limited_r = or * limiter_gain;
  *left = isfinite(limited_l) ? fmaxf(-32768.0f, fminf(32767.0f, limited_l)) : 0.0f;
  *right = isfinite(limited_r) ? fmaxf(-32768.0f, fminf(32767.0f, limited_r)) : 0.0f;
  return limiter_active;
}
