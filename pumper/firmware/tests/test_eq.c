#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "audio_controls.h"
#include "eq_config.h"
#include "eq_dsp.h"
#include "eq_protocol.h"

static void test_default_config(void) {
  eq_config_t config;
  eq_config_set_defaults(&config);
  assert(eq_config_validate(&config));
  assert(eq_config_equal(&config, &k_eq_default_config));
  assert(config.preamp_db == 0.0f);
  for (uint32_t i = 0; i < EQ_NUM_FILTERS; i++) assert(config.filters[i].gain_db == 0.0f);
  config.filters[0].frequency_hz = 5.0f;
  assert(!eq_config_validate(&config));
}

static void test_protocol_header(void) {
  assert(EQ_OPCODE_RESTART_DEVICE == 0x40u);
  assert(EQ_OPCODE_ENTER_BOOTSEL == 0x41u);
  assert(EQ_OPCODE_GET_OUTPUT_PROCESSING == 0x08u);
  assert(EQ_OPCODE_SET_OUTPUT_PROCESSING == 0x13u);
  assert(EQ_OPCODE_SAVE_OUTPUT_PROCESSING == 0x27u);
  assert(EQ_PROTOCOL_STATUS_PAYLOAD_SIZE == 44u);

  uint8_t report[EQ_PROTOCOL_REPORT_SIZE];
  eq_protocol_response_init(report, EQ_OPCODE_GET_STATUS, 0x1234u, EQ_STATUS_OK, 3u);
  report[EQ_PROTOCOL_HEADER_SIZE] = 1u;
  report[EQ_PROTOCOL_HEADER_SIZE + 1u] = 2u;
  report[EQ_PROTOCOL_HEADER_SIZE + 2u] = 3u;
  eq_protocol_packet_t packet;
  assert(eq_protocol_decode(report, sizeof(report), &packet));
  assert(packet.opcode == (EQ_OPCODE_GET_STATUS | EQ_OPCODE_RESPONSE));
  assert(packet.request_id == 0x1234u);
  assert(packet.payload_length == 3u);
  assert(packet.payload[2] == 3u);

  uint8_t signed_value[4];
  eq_protocol_write_i32(signed_value, -12345);
  assert(eq_protocol_read_i32(signed_value) == -12345);

  eq_protocol_response_init(report, EQ_OPCODE_METER_LEVEL, 0u, EQ_STATUS_OK,
                            EQ_PROTOCOL_METER_LEVEL_PAYLOAD_SIZE);
  eq_protocol_write_u32(&report[EQ_PROTOCOL_HEADER_SIZE], 42u);
  eq_protocol_write_u16(&report[EQ_PROTOCOL_HEADER_SIZE + 4u], 32768u);
  eq_protocol_write_u16(&report[EQ_PROTOCOL_HEADER_SIZE + 16u], 16384u);
  assert(eq_protocol_decode(report, sizeof(report), &packet));
  assert(packet.opcode == (EQ_OPCODE_METER_LEVEL | EQ_OPCODE_RESPONSE));
  assert(packet.request_id == 0u);
  assert(packet.payload_length == 28u);
  assert(eq_protocol_read_u32(packet.payload) == 42u);
  assert(eq_protocol_read_u16(packet.payload + 4u) == 32768u);
  assert(eq_protocol_read_u16(packet.payload + 16u) == 16384u);
}

static void test_protocol_config_round_trip(void) {
  eq_config_t decoded = k_eq_default_config;
  uint8_t global[EQ_PROTOCOL_GLOBAL_PAYLOAD_SIZE];
  eq_protocol_encode_global(global, &k_eq_default_config);
  assert(eq_protocol_decode_global(global, sizeof(global), &decoded));
  assert(decoded.preamp_db == k_eq_default_config.preamp_db);

  uint8_t band[EQ_PROTOCOL_BAND_PAYLOAD_SIZE];
  eq_protocol_encode_band(band, 7u, &k_eq_default_config.filters[7]);
  uint8_t index = 0u;
  eq_filter_config_t decoded_band;
  assert(eq_protocol_decode_band(band, sizeof(band), &index, &decoded_band));
  assert(index == 7u);
  assert(decoded_band.frequency_hz == k_eq_default_config.filters[7].frequency_hz);
  assert(decoded_band.gain_db == k_eq_default_config.filters[7].gain_db);

  for (uint8_t type = EQ_FILTER_LOW_PASS; type <= EQ_FILTER_BAND_PASS; ++type) {
    eq_filter_config_t filter = {true, (eq_filter_type_t)type, EQ_WIDTH_Q,
                                 1234.0f, 2.5f, 1.0f, 0.0f};
    eq_protocol_encode_band(band, 0u, &filter);
    assert(eq_protocol_decode_band(band, sizeof(band), &index, &decoded_band));
    assert(decoded_band.type == type && decoded_band.width_mode == EQ_WIDTH_Q);
  }
}

static void test_dsp_bypass_and_processing(void) {
  eq_config_t bypass = k_eq_default_config;
  bypass.enabled = false;
  output_processing_config_t output = k_output_processing_default;
  audio_controls_init(48000u, &k_crossfeed_default, &output);
  float bypass_samples[] = {10000, -10000, 2000, -2000};
  float original[4];
  memcpy(original, bypass_samples, sizeof(original));
  eq_init(48000u, &bypass);
  eq_process_interleaved_stereo(bypass_samples, 2u, NULL, false);
  assert(memcmp(original, bypass_samples, sizeof(original)) == 0);

  eq_config_t preamp = bypass;
  preamp.enabled = true;
  preamp.preamp_db = -6.0206f;
  for (uint32_t i = 0; i < EQ_NUM_FILTERS; i++) preamp.filters[i].enabled = false;
  float samples[] = {10000, -10000};
  eq_init(48000u, &preamp);
  eq_block_metrics_t metrics;
  eq_process_interleaved_stereo(samples, 1u, &metrics, true);
  assert(fabsf(samples[0] - 5000) <= 1);
  assert(fabsf(samples[1] + 5000) <= 1);
  assert(metrics.pre_eq.left_peak == 10000u);
  assert(metrics.pre_eq.left_square_sum == 100000000u);
  assert(metrics.post_eq.left_peak >= 4999u && metrics.post_eq.left_peak <= 5001u);
}

static eq_config_t single_peaking_filter(float frequency_hz) {
  eq_config_t config = k_eq_default_config;
  config.preamp_db = 0.0f;
  for (uint32_t i = 0u; i < EQ_NUM_FILTERS; i++) config.filters[i].enabled = false;
  config.filters[0].enabled = true;
  config.filters[0].type = EQ_FILTER_PEAKING;
  config.filters[0].width_mode = EQ_WIDTH_Q;
  config.filters[0].frequency_hz = frequency_hz;
  config.filters[0].gain_db = 12.0f;
  config.filters[0].q = 10.0f;
  return config;
}

static float process_constant_sample(void) {
  float samples[2] = {10000, 10000};
  eq_process_interleaved_stereo(samples, 1u, NULL, false);
  return samples[0];
}

static void settle_filter(uint32_t frame_count) {
  for (uint32_t i = 0u; i < frame_count; i++) (void)process_constant_sample();
}

static void test_dsp_transition_smoothing(void) {
  uint32_t const sample_rate_hz = 192000u;
  uint32_t const transition_frames = sample_rate_hz / 100u;

  eq_config_t config = single_peaking_filter(1000.0f);
  eq_init(sample_rate_hz, &config);
  settle_filter(sample_rate_hz / 2u);
  config.filters[0].frequency_hz = 1050.0f;
  assert(eq_set_config(&config));
  for (uint32_t i = 0u; i < transition_frames * 2u; i++) {
    assert(fabsf(process_constant_sample() - 10000) <= 32);
  }

  config = single_peaking_filter(100.0f);
  eq_init(sample_rate_hz, &config);
  settle_filter(sample_rate_hz / 2u);
  float previous = process_constant_sample();
  config.filters[0].frequency_hz = 1000.0f;
  assert(eq_set_config(&config));
  for (uint32_t i = 0u; i < transition_frames * 2u; i++) {
    float output = process_constant_sample();
    assert(fabsf(output - previous) <= 16);
    previous = output;
  }

  config = single_peaking_filter(1000.0f);
  eq_init(sample_rate_hz, &config);
  settle_filter(sample_rate_hz / 2u);
  previous = process_constant_sample();
  config.filters[0].frequency_hz = 1050.0f;
  assert(eq_set_config(&config));
  for (uint32_t i = 0u; i < transition_frames / 2u; i++) {
    float output = process_constant_sample();
    assert(fabsf(output - previous) <= 16);
    previous = output;
  }
  config.filters[0].frequency_hz = 1100.0f;
  assert(eq_set_config(&config));
  for (uint32_t i = 0u; i < transition_frames * 2u; i++) {
    float output = process_constant_sample();
    assert(fabsf(output - previous) <= 16);
    previous = output;
  }
}

static float response_rms(eq_filter_type_t type, uint32_t rate, float filter_hz, float tone_hz) {
  eq_config_t config = k_eq_default_config;
  for (uint32_t i = 0; i < EQ_NUM_FILTERS; ++i) config.filters[i].enabled = false;
  config.filters[0] = (eq_filter_config_t){true, type, EQ_WIDTH_Q, filter_hz, 0.707f, 1.0f, 0.0f};
  assert(eq_config_validate(&config));
  eq_init(rate, &config);
  audio_controls_init(rate, &k_crossfeed_default, &k_output_processing_default);
  double sum = 0.0;
  uint32_t measured = rate / 10u;
  for (uint32_t n = 0; n < rate / 5u; ++n) {
    float value = 1000.0f * sinf(2.0f * 3.14159265358979323846f * tone_hz * (float)n /
                                 (float)rate);
    float pair[2] = {value, value};
    eq_process_interleaved_stereo(pair, 1u, NULL, false);
    assert(isfinite(pair[0]) && isfinite(pair[1]));
    if (n >= rate / 10u) sum += (double)pair[0] * pair[0];
  }
  return sqrtf((float)(sum / measured));
}

static void test_new_filter_responses(void) {
  uint32_t rates[] = {44100u, 48000u, 88200u, 96000u, 176400u, 192000u};
  for (size_t i = 0; i < sizeof(rates) / sizeof(rates[0]); ++i) {
    uint32_t rate = rates[i];
    assert(response_rms(EQ_FILTER_LOW_PASS, rate, 1000.0f, 100.0f) > 600.0f);
    assert(response_rms(EQ_FILTER_LOW_PASS, rate, 1000.0f, 10000.0f) < 30.0f);
    assert(response_rms(EQ_FILTER_HIGH_PASS, rate, 1000.0f, 100.0f) < 30.0f);
    assert(response_rms(EQ_FILTER_HIGH_PASS, rate, 1000.0f, 10000.0f) > 600.0f);
    assert(response_rms(EQ_FILTER_NOTCH, rate, 1000.0f, 1000.0f) < 10.0f);
    assert(response_rms(EQ_FILTER_BAND_PASS, rate, 1000.0f, 1000.0f) > 650.0f);
  }
}

int main(void) {
  test_default_config();
  test_protocol_header();
  test_protocol_config_round_trip();
  test_dsp_bypass_and_processing();
  test_dsp_transition_smoothing();
  test_new_filter_responses();
  return 0;
}
