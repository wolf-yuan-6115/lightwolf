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

  eq_protocol_response_init(report, EQ_OPCODE_METER_LEVEL, 0u, EQ_STATUS_OK,
                            EQ_PROTOCOL_METER_LEVEL_PAYLOAD_SIZE);
  eq_protocol_write_u32(&report[EQ_PROTOCOL_HEADER_SIZE], 42u);
  eq_protocol_write_u16(&report[EQ_PROTOCOL_HEADER_SIZE + 4u], 32768u);
  assert(eq_protocol_decode(report, sizeof(report), &packet));
  assert(packet.opcode == (EQ_OPCODE_METER_LEVEL | EQ_OPCODE_RESPONSE));
  assert(packet.request_id == 0u);
  assert(eq_protocol_read_u32(packet.payload) == 42u);
  assert(eq_protocol_read_u16(packet.payload + 4u) == 32768u);
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
  eq_process_interleaved_stereo16(bypass_samples, 2u);
  assert(memcmp(original, bypass_samples, sizeof(original)) == 0);

  eq_config_t preamp = bypass;
  preamp.enabled = true;
  preamp.preamp_db = -6.0206f;
  for (uint32_t i = 0; i < EQ_NUM_FILTERS; i++) preamp.filters[i].enabled = false;
  int16_t samples[] = {10000, -10000};
  eq_init(48000u, &preamp);
  eq_process_interleaved_stereo16(samples, 1u);
  assert(abs(samples[0] - 5000) <= 1);
  assert(abs(samples[1] + 5000) <= 1);
}

int main(void) {
  test_default_config();
  test_protocol_header();
  test_protocol_config_round_trip();
  test_dsp_bypass_and_processing();
  return 0;
}
