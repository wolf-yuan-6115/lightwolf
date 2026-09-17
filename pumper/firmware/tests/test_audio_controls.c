#include "audio_controls.h"
#include "eq_dsp.h"
#include "eq_protocol.h"
#include <assert.h>
#include <math.h>
#include <string.h>
static void settle(unsigned rate) {
  for (unsigned i = 0; i < rate / 100 * 3; i++) {
    float l = 0, r = 0;
    audio_controls_process(&l, &r);
  }
}
static void test_contract(void) {
  uint8_t p[8];
  crossfeed_config_t c;
  crossfeed_encode(p, &k_crossfeed_default);
  assert(p[0] == 0 && p[1] == 0 && p[2] == 0xd0 && p[3] == 7 && p[6] == 250);
  assert(crossfeed_decode(p, 8, &c) && crossfeed_equal(&c, &k_crossfeed_default));
  assert(!crossfeed_decode(p, 7, &c));
  p[1] = 1;
  assert(!crossfeed_decode(p, 8, &c));
  p[1] = 0;
  p[0] = 5;
  assert(!crossfeed_decode(p, 8, &c));
  c = k_crossfeed_default;
  c.strength_bp = 4001;
  assert(!crossfeed_validate(&c));
  c = k_crossfeed_default;
  c.cutoff_hz = 299;
  assert(!crossfeed_validate(&c));
  c.cutoff_hz = 2001;
  assert(!crossfeed_validate(&c));
  c = k_crossfeed_default;
  c.delay_us = 601;
  assert(!crossfeed_validate(&c));
  assert(audio_volume_valid(-12800) && audio_volume_valid(0));
  assert(!audio_volume_valid(-12801) && !audio_volume_valid(-1) && !audio_volume_valid(256));
  int16_t v[3] = {-10 * 256, -3 * 256, 0};
  int8_t m[3] = {0};
  float g[2];
  audio_effective_gains(v, m, g);
  assert(fabsf(g[0] - powf(10, -13.0f / 20)) < 1e-6f);
  assert(fabsf(g[1] - powf(10, -10.0f / 20)) < 1e-6f);
  m[1] = 1;
  audio_effective_gains(v, m, g);
  assert(g[0] == 0 && g[1] > 0);
  m[0] = 1;
  audio_effective_gains(v, m, g);
  assert(g[0] == 0 && g[1] == 0);
}
static void test_host_requests(void) {
  int16_t v[3] = {0};
  int8_t m[3] = {0};
  uint8_t p[9] = {0};
  eq_protocol_write_u16(p, (uint16_t)-12800);
  assert(audio_host_control_set(0, 2, p, 2, v, m) && v[0] == -12800);
  assert(!audio_host_control_set(1, 2, p, 2, v, m));
  assert(!audio_host_control_set(2, 2, p, 2, v, m));
  assert(!audio_host_control_set(1, 1, p, 1, v, m));
  assert(!audio_host_control_set(2, 1, p, 1, v, m));
  assert(!audio_host_control_set(3, 2, p, 2, v, m));
  assert(!audio_host_control_set(255, 1, p, 1, v, m));
  assert(!audio_host_control_set(0, 2, p, 1, v, m));
  assert(!audio_host_control_set(0, 2, p, 3, v, m));
  assert(!audio_host_control_set(0, 2, NULL, 2, v, m));
  eq_protocol_write_u16(p, (uint16_t)-12801);
  assert(!audio_host_control_set(0, 2, p, 2, v, m));
  eq_protocol_write_u16(p, (uint16_t)-1);
  assert(!audio_host_control_set(0, 2, p, 2, v, m));
  eq_protocol_write_u16(p, 256);
  assert(!audio_host_control_set(0, 2, p, 2, v, m));
  assert(v[0] == -12800 && v[1] == 0 && v[2] == 0);
  p[0] = 2;
  assert(!audio_host_control_set(0, 1, p, 1, v, m));
  p[0] = 1;
  assert(!audio_host_control_set(0, 1, p, 2, v, m));
  assert(audio_host_control_set(0, 1, p, 1, v, m) && m[0] == 1);
  assert(!audio_host_control_set(0, 3, p, 1, v, m));
  audio_host_controls_encode(p, v, m);
  assert(p[0] == 0 && p[1] == 0xce && p[2] == 0 && p[3] == 0 && p[4] == 0 && p[5] == 0 &&
         p[6] == 1 && p[7] == 0 && p[8] == 0);
  crossfeed_config_t c = k_crossfeed_default;
  uint8_t state[17];
  crossfeed_state_encode(state, &c, &c);
  assert(state[16] == 0 && !memcmp(state, state + 8, 8));
  c.mode = 3;
  crossfeed_state_encode(state, &c, &k_crossfeed_default);
  assert(state[0] == 3 && state[8] == 0 && state[16] == 1);
  // Preset requests retain Custom fields unchanged in their response.
  assert(!memcmp(state + 1, state + 9, 7));
}
static void test_rate(unsigned rate) {
  audio_controls_init(rate, &k_crossfeed_default);
  assert(audio_controls_bypassed());
  float l = -32768, r = 32767;
  audio_controls_process(&l, &r);
  assert(l == -32768 && r == 32767);
  float gains[2] = {0, 0};
  audio_controls_set_gains(gains);
  float previous = 1;
  for (unsigned i = 0; i < rate / 100; i++) {
    l = r = 1;
    audio_controls_process(&l, &r);
    assert(l <= previous && l >= 0 && l == r);
    previous = l;
    if (i + 1 < rate / 100)
      assert(l > 0);
  }
  assert(l == 0);
  l = r = 32767;
  audio_controls_process(&l, &r);
  assert(l == 0 && r == 0);
  gains[0] = gains[1] = 1;
  audio_controls_set_gains(gains);
  settle(rate);
  assert(audio_controls_bypassed());
  crossfeed_config_t c = {4, 2000, 700, 250};
  audio_controls_set_crossfeed(&c);
  settle(rate);
  // Equal DC reaches unity; unilateral DC has fixed normalization and symmetric leakage.
  for (unsigned i = 0; i < rate / 4; i++) {
    l = 1;
    r = 0;
    audio_controls_process(&l, &r);
  }
  assert(fabsf(l - 1 / 1.2f) < 1e-5f && fabsf(r - 0.2f / 1.2f) < 1e-4f);
  audio_controls_reset(rate);
  float forward_l[512], forward_r[512];
  for (unsigned i = 0; i < 512; i++) {
    l = i == 0 ? 1 : 0;
    r = 0;
    audio_controls_process(&l, &r);
    forward_l[i] = l;
    forward_r[i] = r;
  }
  audio_controls_reset(rate);
  for (unsigned i = 0; i < 512; i++) {
    l = 0;
    r = i == 0 ? 1 : 0;
    audio_controls_process(&l, &r);
    assert(fabsf(r - forward_l[i]) < 1e-7f);
    assert(fabsf(l - forward_r[i]) < 1e-7f);
  }
  unsigned delay = (unsigned)(rate * 0.00025f);
  for (unsigned i = 0; i < delay; i++)
    assert(forward_r[i] == 0);
  assert(forward_r[delay] > 0);
  // Fractional-delay impulse follows linear interpolation of the first-order LP.
  float d = rate * 0.00025f, fraction = d - delay;
  float alpha = 1 - expf(-2 * 3.14159265358979323846f * 700 / rate);
  assert(fabsf(forward_r[delay] - (0.2f / 1.2f) * alpha * (1 - fraction)) < 1e-6f);
  // Alternating input is strongly attenuated in the opposite channel.
  audio_controls_reset(rate);
  for (unsigned i = 0; i < rate / 4; i++) {
    l = i % 2 ? 1 : -1;
    r = 0;
    audio_controls_process(&l, &r);
  }
  assert(fabsf(r) <= (0.2f / 1.2f) * alpha / (2 - alpha) + 1e-6f);
  // Fade off keeps processing until the tenth millisecond and lands on exact bypass.
  audio_controls_set_crossfeed(&k_crossfeed_default);
  assert(!audio_controls_bypassed());
  for (unsigned i = 0; i < rate / 100; i++) {
    l = 1;
    r = 0;
    audio_controls_process(&l, &r);
    assert(audio_controls_bypassed() == (i + 1 == rate / 100));
  }
  assert(audio_controls_bypassed());
  c.mode = 3;
  audio_controls_set_crossfeed(&c);
  c.mode = 1;
  audio_controls_set_crossfeed(&c);
  settle(rate);
  l = 1;
  r = 0;
  for (unsigned i = 0; i < rate / 4; i++) {
    l = 1;
    r = 0;
    audio_controls_process(&l, &r);
  }
  assert(fabsf(l - 1 / 1.1f) < 1e-6f && fabsf(r - 0.1f / 1.1f) < 1e-4f);
  // Restart and rate changes clear prior opposite-channel history.
  audio_controls_reset(rate);
  l = r = 0;
  audio_controls_process(&l, &r);
  assert(l == 0 && r == 0);
  c = (crossfeed_config_t){4, 4000, 2000, 600};
  audio_controls_set_crossfeed(&c);
  settle(rate);
  for (unsigned i = 0; i < 1024; i++) {
    l = 32767;
    r = -32768;
    audio_controls_process(&l, &r);
    assert(isfinite(l) && isfinite(r));
  }
}
static void test_final_meter(void) {
  eq_config_t eq = k_eq_default_config;
  eq.preamp_db = 6;
  eq_init(48000, &eq);
  audio_controls_init(48000, &k_crossfeed_default);
  float g[2] = {0.1f, 0.1f};
  audio_controls_set_gains(g);
  settle(48000);
  int16_t samples[2] = {30000, -30000};
  eq_block_metrics_t m;
  eq_process_interleaved_stereo16(samples, 1, &m, true);
  // Gain happens before saturation, so EQ does not clip to 32767 first.
  assert(samples[0] > 5900 && samples[0] < 6100 && samples[1] == -samples[0]);
  assert(m.post_eq.left_peak == samples[0]);
  g[0] = g[1] = 1;
  audio_controls_set_gains(g);
  settle(48000);
  samples[0] = 30000;
  samples[1] = -30000;
  eq_process_interleaved_stereo16(samples, 1, &m, true);
  assert(samples[0] == 32767 && samples[1] == -32768);
  assert(m.post_eq.left_peak == 32767 && m.post_eq.right_peak == 32768);
  eq.enabled = false;
  eq_init(48000, &eq);
  crossfeed_config_t c = {2, 2000, 700, 250};
  audio_controls_set_crossfeed(&c);
  settle(48000);
  for (unsigned i = 0; i < 5000; i++) {
    samples[0] = 10000;
    samples[1] = 0;
    eq_process_interleaved_stereo16(samples, 1, NULL, false);
  }
  assert(samples[1] > 1600); // Independent of EQ enable.
}
static void test_full_pipeline(unsigned rate) {
  eq_config_t eq = k_eq_default_config;
  eq.preamp_db = -12;
  for (unsigned i = 0; i < EQ_NUM_FILTERS; i++) {
    eq.filters[i].type = EQ_FILTER_PEAKING;
    eq.filters[i].width_mode = EQ_WIDTH_Q;
    eq.filters[i].q = 1;
    eq.filters[i].frequency_hz = 40.0f * powf(1.8f, (float)i);
    eq.filters[i].gain_db = i % 2 ? -6 : 6;
  }
  assert(eq_config_validate(&eq));
  eq_init(rate, &eq);
  crossfeed_config_t c = {CROSSFEED_CUSTOM, 4000, 2000, 600};
  audio_controls_init(rate, &c);
  float g[2] = {0.5f, 0.25f};
  audio_controls_set_gains(g);
  uint32_t random = 12345;
  for (unsigned block = 0; block < 250; block++) {
    int16_t samples[384];
    for (unsigned i = 0; i < 384; i++) {
      random = random * 1664525u + 1013904223u;
      samples[i] = (int16_t)(random >> 16);
    }
    eq_block_metrics_t metrics;
    eq_process_interleaved_stereo16(samples, 192, &metrics, true);
    uint16_t peaks[2] = {0};
    uint64_t squares[2] = {0};
    for (unsigned i = 0; i < 384; i++) {
      int32_t magnitude = samples[i] < 0 ? -(int32_t)samples[i] : samples[i];
      if (magnitude > peaks[i % 2])
        peaks[i % 2] = (uint16_t)magnitude;
      squares[i % 2] += (uint64_t)magnitude * magnitude;
    }
    assert(metrics.post_eq.left_peak == peaks[0] && metrics.post_eq.right_peak == peaks[1]);
    assert(metrics.post_eq.left_square_sum == squares[0] &&
           metrics.post_eq.right_square_sum == squares[1]);
  }
}
int main(void) {
  test_contract();
  test_host_requests();
  unsigned rates[] = {8000, 11025, 16000, 22050, 32000, 44100, 48000, 88200, 96000, 176400, 192000};
  for (unsigned i = 0; i < sizeof(rates) / sizeof(rates[0]); i++)
    test_rate(rates[i]);
  test_final_meter();
  unsigned advertised[] = {44100, 48000, 96000, 192000};
  for (unsigned i = 0; i < 4; i++)
    test_full_pipeline(advertised[i]);
  return 0;
}
