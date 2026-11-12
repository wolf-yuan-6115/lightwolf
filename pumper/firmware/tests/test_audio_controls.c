#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "audio_controls.h"
#include "eq_dsp.h"
#include "eq_protocol.h"

static void settle(unsigned rate) {
  for (unsigned i = 0; i < rate / 100u; ++i) {
    double l = 0.0, r = 0.0;
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
  double gain[2];
  audio_effective_gains(volume, mute, false, gain);
  assert(gain[0] == 0.0 && gain[1] == 0.0);
  audio_effective_gains(volume, mute, true, gain);
  assert(gain[0] == 0.0 && gain[1] == 0.0);
  mute[0] = 0;
  audio_effective_gains(volume, mute, true, gain);
  assert(gain[0] == 1.0 && gain[1] == 1.0);
  volume[0] = -2560;
  audio_effective_gains(volume, mute, true, gain);
  assert(gain[0] == 1.0 && gain[1] == 1.0);
  audio_effective_gains(volume, mute, false, gain);
  assert(fabs(gain[0] - pow(10.0, -0.5)) < 0.00001);
}

static void process_after_settle(unsigned rate, double in_l, double in_r, double *left,
                                 double *right) {
  settle(rate);
  *left = in_l;
  *right = in_r;
  audio_controls_process(left, right);
}

static void test_output_utilities(void) {
  unsigned const rate = 48000u;
  audio_controls_init(rate, &k_crossfeed_default, &k_output_processing_default);
  output_processing_config_t config = k_output_processing_default;
  double l, r;

  config.flags = OUTPUT_PROCESSING_SWAP;
  audio_controls_set_output_processing(&config);
  process_after_settle(rate, 1000.0, 250.0, &l, &r);
  assert(fabs(l - 250.0) < 0.1 && fabs(r - 1000.0) < 0.1);

  config.flags = OUTPUT_PROCESSING_INVERT_LEFT | OUTPUT_PROCESSING_INVERT_RIGHT;
  audio_controls_set_output_processing(&config);
  process_after_settle(rate, 1000.0, 250.0, &l, &r);
  assert(fabs(l + 1000.0) < 0.1 && fabs(r + 250.0) < 0.1);

  config.flags = OUTPUT_PROCESSING_SWAP | OUTPUT_PROCESSING_INVERT_LEFT;
  audio_controls_set_output_processing(&config);
  process_after_settle(rate, 1000.0, 250.0, &l, &r);
  assert(fabs(l + 250.0) < 0.1 && fabs(r - 1000.0) < 0.1);

  config.flags = OUTPUT_PROCESSING_MONO;
  audio_controls_set_output_processing(&config);
  process_after_settle(rate, 1000.0, 0.0, &l, &r);
  assert(fabs(l - 500.0) < 0.1 && fabs(r - 500.0) < 0.1);

  config.flags = 0u;
  config.width_bp = 20000u;
  audio_controls_set_output_processing(&config);
  process_after_settle(rate, 1000.0, 0.0, &l, &r);
  assert(fabs(l - 1500.0) < 0.1 && fabs(r + 500.0) < 0.1);

  config.width_bp = 10000u;
  config.balance_bp = 5000;
  audio_controls_set_output_processing(&config);
  process_after_settle(rate, 1000.0, 1000.0, &l, &r);
  assert(fabs(l - 500.0) < 0.1 && fabs(r - 1000.0) < 0.1);

  config.balance_bp = -5000;
  audio_controls_set_output_processing(&config);
  for (unsigned i = 0; i < rate / 200u; ++i) {
    l = r = 0.0;
    audio_controls_process(&l, &r);
  }
  config.balance_bp = 0;
  config.flags = OUTPUT_PROCESSING_SWAP;
  audio_controls_set_output_processing(&config);
  process_after_settle(rate, 1000.0, 250.0, &l, &r);
  assert(fabs(l - 250.0) < 0.1 && fabs(r - 1000.0) < 0.1);
}

static void test_limiter(void) {
  unsigned const rate = 48000u;
  audio_controls_init(rate, &k_crossfeed_default, &k_output_processing_default);

  double l = 32767.0, r = -32768.0;
  assert(!audio_controls_process(&l, &r));
  assert(l == 32767.0 && r == -32768.0);

  l = 32768.0;
  r = -16384.0;
  assert(!audio_controls_process(&l, &r));
  assert(l == 32768.0 && r == -16384.0);

  l = 65536.0;
  r = -32768.0;
  assert(audio_controls_process(&l, &r));
  assert(l == 32768.0 && r == -16384.0);

  audio_controls_reset(rate);
  l = 20000.0;
  r = -10000.0;
  assert(!audio_controls_process(&l, &r));
  assert(l == 20000.0 && r == -10000.0);

  l = 60000.0;
  r = -30000.0;
  assert(audio_controls_process(&l, &r));
  assert(fabs(l) <= 32768.0 && fabs(r) <= 32768.0);
  assert(fabs(l / r + 2.0) < 0.001);

  double first_gain = fabs(l / 60000.0);
  l = r = 1000.0;
  assert(audio_controls_process(&l, &r));
  double early_gain = l / 1000.0;
  assert(early_gain >= first_gain && early_gain < 1.0);
  bool active = true;
  for (unsigned i = 0; i < rate; ++i) {
    l = r = 1000.0;
    active = audio_controls_process(&l, &r);
  }
  assert(l > 999.0 && l <= 1000.0);
  assert(!active);
  l = r = 0.0;
  audio_controls_process(&l, &r);
  assert(l == 0.0 && r == 0.0);

  eq_init(rate, &k_eq_default_config);
  audio_controls_reset(rate);
  double block[] = {60000.0, -30000.0, 1000.0, 1000.0};
  eq_block_metrics_t metrics;
  eq_process_interleaved_stereo(block, 2u, &metrics, true);
  assert(metrics.limiter_active_frames > 0u);
}

static void test_crossfeed_and_pipeline(void) {
  unsigned const rate = 48000u;
  crossfeed_config_t crossfeed = {CROSSFEED_CUSTOM, 2000, 700, 250};
  audio_controls_init(rate, &crossfeed, &k_output_processing_default);
  double l = 0.0, r = 0.0;
  for (unsigned i = 0; i < rate / 4u; ++i) {
    l = 1000.0;
    r = 0.0;
    audio_controls_process(&l, &r);
  }
  assert(fabs(l - 1000.0 / 1.2) < 0.1);
  assert(fabs(r - 200.0 / 1.2) < 0.1);

  eq_config_t eq = k_eq_default_config;
  eq.preamp_db = -6.0206;
  for (unsigned i = 0; i < EQ_NUM_FILTERS; ++i) eq.filters[i].enabled = false;
  eq_init(rate, &eq);
  audio_controls_init(rate, &k_crossfeed_default, &k_output_processing_default);
  double samples[2] = {10000.0, -10000.0};
  eq_block_metrics_t metrics;
  eq_process_interleaved_stereo(samples, 1u, &metrics, true);
  assert(fabs(samples[0] - 5000.0) < 1.0);
  assert(metrics.pre_eq.left_peak == 10000u);
  assert(metrics.post_eq.left_peak >= 4999u && metrics.post_eq.left_peak <= 5001u);
}

static void test_control_precision_and_ramps(void) {
  unsigned const rate = 96000u;
  double const input = 16384.0 + 1.0 / 65536.0;
  audio_controls_init(rate, &k_crossfeed_default, &k_output_processing_default);
  double gains[] = {0.5, 0.5};
  audio_controls_set_gains(gains);
  for (unsigned i = 1; i <= rate / 100u; ++i) {
    double l = input, r = -input;
    assert(!audio_controls_process(&l, &r));
    double expected = input * (1.0 - 0.5 * i / (rate / 100u));
    assert(fabs(l - expected) < 1e-10 && l == -r);
  }
  gains[0] = gains[1] = 0.0;
  audio_controls_set_gains(gains);
  settle(rate);
  double l = input, r = input;
  assert(!audio_controls_process(&l, &r));
  assert(l == 0.0 && r == 0.0);

  crossfeed_config_t crossfeed = {CROSSFEED_CUSTOM, 2000, 700, 253};
  audio_controls_init(rate, &crossfeed, &k_output_processing_default);
  for (unsigned i = 0; i < rate / 4u; ++i) {
    l = input; r = 0.0;
    assert(!audio_controls_process(&l, &r));
  }
  assert(fabs(l - input / 1.2) < 1e-9);
  assert(fabs(r - input * 0.2 / 1.2) < 1e-9);
}

int main(void) {
  test_wire_contracts();
  test_host_volume();
  test_output_utilities();
  test_limiter();
  test_crossfeed_and_pipeline();
  test_control_precision_and_ramps();
  return 0;
}
