#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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
}

static void test_dsp_bypass_and_processing(void) {
  eq_config_t bypass = k_eq_default_config;
  bypass.enabled = false;
  int16_t bypass_samples[] = {10000, -10000, 2000, -2000};
  int16_t original[4];
  memcpy(original, bypass_samples, sizeof(original));
  eq_init(48000u, &bypass);
  eq_process_interleaved_stereo16(bypass_samples, 2u, NULL, false);
  assert(memcmp(original, bypass_samples, sizeof(original)) == 0);

  eq_config_t preamp = bypass;
  preamp.enabled = true;
  preamp.preamp_db = -6.0206f;
  for (uint32_t i = 0; i < EQ_NUM_FILTERS; i++) preamp.filters[i].enabled = false;
  int16_t samples[] = {10000, -10000};
  eq_init(48000u, &preamp);
  eq_block_metrics_t metrics;
  eq_process_interleaved_stereo16(samples, 1u, &metrics, true);
  assert(abs(samples[0] - 5000) <= 1);
  assert(abs(samples[1] + 5000) <= 1);
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

static int16_t process_constant_sample(void) {
  int16_t samples[2] = {10000, 10000};
  eq_process_interleaved_stereo16(samples, 1u, NULL, false);
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
    assert(abs(process_constant_sample() - 10000) <= 32);
  }

  config = single_peaking_filter(100.0f);
  eq_init(sample_rate_hz, &config);
  settle_filter(sample_rate_hz / 2u);
  int previous = process_constant_sample();
  config.filters[0].frequency_hz = 1000.0f;
  assert(eq_set_config(&config));
  for (uint32_t i = 0u; i < transition_frames * 2u; i++) {
    int output = process_constant_sample();
    assert(abs(output - previous) <= 16);
    previous = output;
  }

  config = single_peaking_filter(1000.0f);
  eq_init(sample_rate_hz, &config);
  settle_filter(sample_rate_hz / 2u);
  previous = process_constant_sample();
  config.filters[0].frequency_hz = 1050.0f;
  assert(eq_set_config(&config));
  for (uint32_t i = 0u; i < transition_frames / 2u; i++) {
    int output = process_constant_sample();
    assert(abs(output - previous) <= 16);
    previous = output;
  }
  config.filters[0].frequency_hz = 1100.0f;
  assert(eq_set_config(&config));
  for (uint32_t i = 0u; i < transition_frames * 2u; i++) {
    int output = process_constant_sample();
    assert(abs(output - previous) <= 16);
    previous = output;
  }
}

int main(void) {
  test_default_config();
  test_protocol_header();
  test_protocol_config_round_trip();
  test_dsp_bypass_and_processing();
  test_dsp_transition_smoothing();
  return 0;
}
