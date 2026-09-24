#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "audio_controls.h"
#include "eq_dsp.h"
#include "eq_protocol.h"

static void settle(unsigned rate) {
  for (unsigned i = 0; i < rate / 100u; ++i) {
    float l = 0.0f, r = 0.0f;
    audio_controls_process(&l, &r);
  }
}

static void test_wire_contracts(void) {
  uint8_t payload[OUTPUT_PROCESSING_STATE_SIZE] = {0};
  crossfeed_config_t crossfeed;
  crossfeed_encode(payload, &k_crossfeed_default);
  assert(crossfeed_decode(payload, CROSSFEED_RECORD_SIZE, &crossfeed));
  assert(crossfeed_equal(&crossfeed, &k_crossfeed_default));
  assert(!crossfeed_decode(payload, CROSSFEED_RECORD_SIZE - 1u, &crossfeed));

  output_processing_config_t config = {
      OUTPUT_PROCESSING_SWAP | OUTPUT_PROCESSING_INVERT_LEFT, -2500, 15000};
  output_processing_encode(payload, &config);
  output_processing_config_t decoded;
  assert(output_processing_decode(payload, OUTPUT_PROCESSING_RECORD_SIZE, &decoded));
  assert(output_processing_equal(&config, &decoded));
  payload[7] = 1u;
  assert(!output_processing_decode(payload, OUTPUT_PROCESSING_RECORD_SIZE, &decoded));
  config.balance_bp = -10001;
  assert(!output_processing_validate(&config));
  config.balance_bp = 0;
  config.width_bp = 20001;
  assert(!output_processing_validate(&config));

  output_processing_state_encode(payload, &k_output_processing_default,
                                  &k_output_processing_default);
  assert(payload[16] == 0u);
  config = k_output_processing_default;
  config.flags = OUTPUT_PROCESSING_MONO;
  output_processing_state_encode(payload, &config, &k_output_processing_default);
  assert(payload[16] == 1u);
}

static void test_host_volume(void) {
  int16_t volume[3] = {0};
  int8_t mute[3] = {0};
  uint8_t payload[9] = {0};
  eq_protocol_write_u16(payload, (uint16_t)-12800);
  assert(audio_host_control_set(0, 2, payload, 2, volume, mute));
  assert(volume[0] == -12800);
  assert(!audio_host_control_set(1, 2, payload, 2, volume, mute));
  payload[0] = 1;
  assert(audio_host_control_set(0, 1, payload, 1, volume, mute));
  assert(mute[0] == 1);
  float gain[2];
  audio_effective_gains(volume, mute, gain);
  assert(gain[0] == 0.0f && gain[1] == 0.0f);
}

static void process_after_settle(unsigned rate, float in_l, float in_r, float *left,
                                 float *right) {
  settle(rate);
  *left = in_l;
  *right = in_r;
  audio_controls_process(left, right);
}

static void test_output_utilities(void) {
  unsigned const rate = 48000u;
  audio_controls_init(rate, &k_crossfeed_default, &k_output_processing_default);
  output_processing_config_t config = k_output_processing_default;
  float l, r;

  config.flags = OUTPUT_PROCESSING_SWAP;
  audio_controls_set_output_processing(&config);
  process_after_settle(rate, 1000.0f, 250.0f, &l, &r);
  assert(fabsf(l - 250.0f) < 0.1f && fabsf(r - 1000.0f) < 0.1f);

  config.flags = OUTPUT_PROCESSING_INVERT_LEFT | OUTPUT_PROCESSING_INVERT_RIGHT;
  audio_controls_set_output_processing(&config);
  process_after_settle(rate, 1000.0f, 250.0f, &l, &r);
  assert(fabsf(l + 1000.0f) < 0.1f && fabsf(r + 250.0f) < 0.1f);

  config.flags = OUTPUT_PROCESSING_SWAP | OUTPUT_PROCESSING_INVERT_LEFT;
  audio_controls_set_output_processing(&config);
  process_after_settle(rate, 1000.0f, 250.0f, &l, &r);
  assert(fabsf(l + 250.0f) < 0.1f && fabsf(r - 1000.0f) < 0.1f);

  config.flags = OUTPUT_PROCESSING_MONO;
  audio_controls_set_output_processing(&config);
  process_after_settle(rate, 1000.0f, 0.0f, &l, &r);
  assert(fabsf(l - 500.0f) < 0.1f && fabsf(r - 500.0f) < 0.1f);

  config.flags = 0u;
  config.width_bp = 20000u;
  audio_controls_set_output_processing(&config);
  process_after_settle(rate, 1000.0f, 0.0f, &l, &r);
  assert(fabsf(l - 1500.0f) < 0.1f && fabsf(r + 500.0f) < 0.1f);

  config.width_bp = 10000u;
  config.balance_bp = 5000;
  audio_controls_set_output_processing(&config);
  process_after_settle(rate, 1000.0f, 1000.0f, &l, &r);
  assert(fabsf(l - 500.0f) < 0.1f && fabsf(r - 1000.0f) < 0.1f);

  config.balance_bp = -5000;
  audio_controls_set_output_processing(&config);
  for (unsigned i = 0; i < rate / 200u; ++i) {
    l = r = 0.0f;
    audio_controls_process(&l, &r);
  }
  config.balance_bp = 0;
  config.flags = OUTPUT_PROCESSING_SWAP;
  audio_controls_set_output_processing(&config);
  process_after_settle(rate, 1000.0f, 250.0f, &l, &r);
  assert(fabsf(l - 250.0f) < 0.1f && fabsf(r - 1000.0f) < 0.1f);
}

static void test_limiter(void) {
  unsigned const rate = 48000u;
  audio_controls_init(rate, &k_crossfeed_default, &k_output_processing_default);

  float l = 32767.0f, r = -32768.0f;
  assert(!audio_controls_process(&l, &r));
  assert(l == 32767.0f && r == -32768.0f);

  l = 32768.0f;
  r = -16384.0f;
  assert(!audio_controls_process(&l, &r));
  assert(l == 32767.0f && r == -16384.0f);

  l = 65536.0f;
  r = -32768.0f;
  assert(audio_controls_process(&l, &r));
  assert(l == 32767.0f && r == -16384.0f);

  audio_controls_reset(rate);
  l = 20000.0f;
  r = -10000.0f;
  assert(!audio_controls_process(&l, &r));
  assert(l == 20000.0f && r == -10000.0f);

  l = 60000.0f;
  r = -30000.0f;
  assert(audio_controls_process(&l, &r));
  assert(fabsf(l) < 32768.0f && fabsf(r) < 32768.0f);
  assert(fabsf(l / r + 2.0f) < 0.001f);

  float first_gain = fabsf(l / 60000.0f);
  l = r = 1000.0f;
  assert(audio_controls_process(&l, &r));
  float early_gain = l / 1000.0f;
  assert(early_gain >= first_gain && early_gain < 1.0f);
  bool active = true;
  for (unsigned i = 0; i < rate; ++i) {
    l = r = 1000.0f;
    active = audio_controls_process(&l, &r);
  }
  assert(l > 999.0f && l <= 1000.0f);
  assert(!active);
  l = r = 0.0f;
  audio_controls_process(&l, &r);
  assert(l == 0.0f && r == 0.0f);

  eq_init(rate, &k_eq_default_config);
  audio_controls_reset(rate);
  float block[] = {60000.0f, -30000.0f, 1000.0f, 1000.0f};
  eq_block_metrics_t metrics;
  eq_process_interleaved_stereo(block, 2u, &metrics, true);
  assert(metrics.limiter_active);
}

static void test_crossfeed_and_pipeline(void) {
  unsigned const rate = 48000u;
  crossfeed_config_t crossfeed = {CROSSFEED_CUSTOM, 2000, 700, 250};
  audio_controls_init(rate, &crossfeed, &k_output_processing_default);
  float l = 0.0f, r = 0.0f;
  for (unsigned i = 0; i < rate / 4u; ++i) {
    l = 1000.0f;
    r = 0.0f;
    audio_controls_process(&l, &r);
  }
  assert(fabsf(l - 1000.0f / 1.2f) < 0.1f);
  assert(fabsf(r - 200.0f / 1.2f) < 0.1f);

  eq_config_t eq = k_eq_default_config;
  eq.preamp_db = -6.0206f;
  for (unsigned i = 0; i < EQ_NUM_FILTERS; ++i) eq.filters[i].enabled = false;
  eq_init(rate, &eq);
  audio_controls_init(rate, &k_crossfeed_default, &k_output_processing_default);
  float samples[2] = {10000.0f, -10000.0f};
  eq_block_metrics_t metrics;
  eq_process_interleaved_stereo(samples, 1u, &metrics, true);
  assert(fabsf(samples[0] - 5000.0f) < 1.0f);
  assert(metrics.pre_eq.left_peak == 10000u);
  assert(metrics.post_eq.left_peak >= 4999u && metrics.post_eq.left_peak <= 5001u);
}

int main(void) {
  test_wire_contracts();
  test_host_volume();
  test_output_utilities();
  test_limiter();
  test_crossfeed_and_pipeline();
  return 0;
}
