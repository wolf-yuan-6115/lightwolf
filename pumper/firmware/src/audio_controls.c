#include "audio_controls.h"
#include "eq_protocol.h"
#include <math.h>
#include <string.h>
#define DELAY_SIZE 128u
#define PI 3.14159265358979323846f
_Static_assert(DELAY_SIZE > (192000u * 600u / 1000000u) + 1u, "Delay history too small");
_Static_assert(CROSSFEED_RECORD_SIZE == EQ_PROTOCOL_CROSSFEED_PAYLOAD_SIZE, "Record size mismatch");
_Static_assert(CROSSFEED_STATE_SIZE == EQ_PROTOCOL_CROSSFEED_STATE_PAYLOAD_SIZE,
               "State size mismatch");
crossfeed_config_t const k_crossfeed_default = {CROSSFEED_OFF, 2000, 700, 250};
bool crossfeed_validate(crossfeed_config_t const *c) {
  return c && c->mode <= CROSSFEED_CUSTOM && c->strength_bp <= 4000 && c->cutoff_hz >= 300 &&
         c->cutoff_hz <= 2000 && c->delay_us <= 600;
}
bool crossfeed_equal(crossfeed_config_t const *a, crossfeed_config_t const *b) {
  return a->mode == b->mode && a->strength_bp == b->strength_bp && a->cutoff_hz == b->cutoff_hz &&
         a->delay_us == b->delay_us;
}
void crossfeed_encode(uint8_t *p, crossfeed_config_t const *c) {
  p[0] = c->mode;
  p[1] = 0;
  eq_protocol_write_u16(p + 2, c->strength_bp);
  eq_protocol_write_u16(p + 4, c->cutoff_hz);
  eq_protocol_write_u16(p + 6, c->delay_us);
}
bool crossfeed_decode(uint8_t const *p, size_t n, crossfeed_config_t *c) {
  if (!p || !c || n != CROSSFEED_RECORD_SIZE || p[1])
    return false;
  crossfeed_config_t next = {p[0], eq_protocol_read_u16(p + 2), eq_protocol_read_u16(p + 4),
                             eq_protocol_read_u16(p + 6)};
  if (!crossfeed_validate(&next))
    return false;
  *c = next;
  return true;
}
bool audio_volume_valid(int16_t v) { return v >= -12800 && v <= 0 && v % 256 == 0; }
bool audio_host_control_set(uint8_t channel, uint8_t selector, uint8_t const *p, size_t length,
                            int16_t volume[3], int8_t mute[3]) {
  if (channel != 0u || !p || !volume || !mute)
    return false;
  if (selector == 1u && length == 1u && p[0] <= 1u) {
    mute[channel] = (int8_t)p[0];
    return true;
  }
  if (selector == 2u && length == 2u) {
    int16_t value = (int16_t)eq_protocol_read_u16(p);
    if (!audio_volume_valid(value))
      return false;
    volume[channel] = value;
    return true;
  }
  return false;
}
void audio_host_controls_encode(uint8_t *p, int16_t const v[3], int8_t const m[3]) {
  for (unsigned i = 0; i < 3; i++) {
    eq_protocol_write_u16(p + 2 * i, (uint16_t)v[i]);
    p[6 + i] = (uint8_t)m[i];
  }
}
void crossfeed_state_encode(uint8_t *p, crossfeed_config_t const *live,
                            crossfeed_config_t const *saved) {
  crossfeed_encode(p, live);
  crossfeed_encode(p + CROSSFEED_RECORD_SIZE, saved);
  p[16] = !crossfeed_equal(live, saved);
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
// The active and target states keep separate LP/delay histories through a fade.
// A third static state holds coalesced parameters; coefficients are calculated
// only from setters, outside the sample loop.
static crossfeed_processor_t current, next, pending;
static bool has_pending;
static crossfeed_config_t config;
static uint32_t rate_hz, ramp_frames, fade_remaining, gain_remaining;
static float gain[2] = {1, 1}, target_gain[2] = {1, 1}, gain_step[2];
static void configure(crossfeed_processor_t *s, crossfeed_config_t const *c) {
  static const uint16_t strengths[] = {0, 1000, 2000, 3000};
  static const uint16_t delays[] = {0, 200, 250, 300};
  float strength = (c->mode == CROSSFEED_CUSTOM ? c->strength_bp : strengths[c->mode]) / 10000.0f;
  unsigned cutoff = c->mode == CROSSFEED_CUSTOM ? c->cutoff_hz : 700;
  unsigned us = c->mode == CROSSFEED_CUSTOM ? c->delay_us : delays[c->mode];
  s->strength = strength;
  s->normalizer = 1.0f / (1.0f + strength);
  s->alpha = 1.0f - expf(-2.0f * PI * (float)cutoff / (float)rate_hz);
  float delay = (float)us * (float)rate_hz / 1000000.0f;
  s->delay = (unsigned)delay;
  s->fraction = delay - (float)s->delay;
}
void audio_controls_init(uint32_t rate, crossfeed_config_t const *c) {
  config = *c;
  gain[0] = gain[1] = target_gain[0] = target_gain[1] = 1;
  gain_remaining = 0;
  audio_controls_reset(rate);
}
void audio_controls_reset(uint32_t rate) {
  rate_hz = rate ? rate : 48000;
  ramp_frames = rate_hz / 100;
  if (!ramp_frames)
    ramp_frames = 1;
  memset(&current, 0, sizeof(current));
  configure(&current, &config);
  next = current;
  fade_remaining = 0;
  has_pending = false;
  for (unsigned i = 0; i < 2; i++)
    gain[i] = target_gain[i];
  gain_remaining = 0;
}
void audio_controls_set_crossfeed(crossfeed_config_t const *c) {
  if (!crossfeed_validate(c) || crossfeed_equal(&config, c))
    return;
  config = *c;
  if (fade_remaining) {
    pending = current;
    configure(&pending, c);
    has_pending = true;
  } else {
    next = current;
    configure(&next, c);
    fade_remaining = ramp_frames;
  }
}
void audio_controls_set_gains(float const g[2]) {
  if (g[0] == target_gain[0] && g[1] == target_gain[1])
    return;
  for (unsigned i = 0; i < 2; i++) {
    target_gain[i] = g[i];
    gain_step[i] = (g[i] - gain[i]) / (float)ramp_frames;
  }
  gain_remaining = ramp_frames;
}
bool audio_controls_bypassed(void) {
  return !fade_remaining && current.strength == 0 && !gain_remaining && gain[0] == 1 &&
         gain[1] == 1;
}
static void process_crossfeed(crossfeed_processor_t *s, float l, float r, float *out_l,
                              float *out_r) {
  // Both ear outputs derive from the original pair, never a modified channel.
  float input[2] = {l, r}, delayed[2];
  for (unsigned i = 0; i < 2; i++) {
    s->low[i] += s->alpha * (input[i] - s->low[i]);
    s->history[i][s->position] = s->low[i];
    unsigned a = (s->position + DELAY_SIZE - s->delay) % DELAY_SIZE;
    unsigned b = (a + DELAY_SIZE - 1) % DELAY_SIZE;
    delayed[i] = s->history[i][a] + s->fraction * (s->history[i][b] - s->history[i][a]);
  }
  s->position = (s->position + 1) % DELAY_SIZE;
  if (s->strength == 0) {
    *out_l = l;
    *out_r = r;
    return;
  }
  *out_l = (l + s->strength * delayed[1]) * s->normalizer;
  *out_r = (r + s->strength * delayed[0]) * s->normalizer;
}
void audio_controls_process(float *l, float *r) {
  float a = *l, b = *r;
  if (current.strength != 0 || fade_remaining)
    process_crossfeed(&current, *l, *r, &a, &b);
  if (fade_remaining) {
    float x, y;
    process_crossfeed(&next, *l, *r, &x, &y);
    float blend = (float)(ramp_frames - fade_remaining + 1) / (float)ramp_frames;
    a += blend * (x - a);
    b += blend * (y - b);
    if (!--fade_remaining) {
      current = next;
      if (has_pending) {
        next = current;
        next.strength = pending.strength;
        next.normalizer = pending.normalizer;
        next.alpha = pending.alpha;
        next.delay = pending.delay;
        next.fraction = pending.fraction;
        has_pending = false;
        fade_remaining = ramp_frames;
      }
    }
  }
  if (gain_remaining) {
    --gain_remaining;
    for (unsigned i = 0; i < 2; i++)
      gain[i] = target_gain[i] - gain_step[i] * (float)gain_remaining;
  }
  *l = gain[0] == 0 ? 0 : gain[0] == 1 ? a : a * gain[0];
  *r = gain[1] == 0 ? 0 : gain[1] == 1 ? b : b * gain[1];
}
