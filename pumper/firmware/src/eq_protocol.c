#include "eq_protocol.h"

#include <math.h>
#include <string.h>

#define EQ_MAGIC_0 ((uint8_t)'P')
#define EQ_MAGIC_1 ((uint8_t)'E')

uint16_t eq_protocol_read_u16(uint8_t const *data) {
  return (uint16_t)data[0] | ((uint16_t)data[1] << 8u);
}

uint32_t eq_protocol_read_u32(uint8_t const *data) {
  return (uint32_t)data[0] | ((uint32_t)data[1] << 8u) | ((uint32_t)data[2] << 16u) |
         ((uint32_t)data[3] << 24u);
}

int32_t eq_protocol_read_i32(uint8_t const *data) {
  return (int32_t)eq_protocol_read_u32(data);
}

void eq_protocol_write_u16(uint8_t *data, uint16_t value) {
  data[0] = (uint8_t)(value & 0xffu);
  data[1] = (uint8_t)(value >> 8u);
}

void eq_protocol_write_u32(uint8_t *data, uint32_t value) {
  data[0] = (uint8_t)(value & 0xffu);
  data[1] = (uint8_t)((value >> 8u) & 0xffu);
  data[2] = (uint8_t)((value >> 16u) & 0xffu);
  data[3] = (uint8_t)((value >> 24u) & 0xffu);
}

void eq_protocol_write_i32(uint8_t *data, int32_t value) {
  eq_protocol_write_u32(data, (uint32_t)value);
}

bool eq_protocol_decode(uint8_t const *report, size_t length, eq_protocol_packet_t *packet) {
  if (report == NULL || packet == NULL || length != EQ_PROTOCOL_REPORT_SIZE || report[0] != EQ_MAGIC_0 ||
      report[1] != EQ_MAGIC_1 || report[2] != EQ_PROTOCOL_VERSION || report[6] > EQ_PROTOCOL_PAYLOAD_SIZE) {
    return false;
  }
  packet->opcode = report[3];
  packet->request_id = eq_protocol_read_u16(&report[4]);
  packet->payload_length = report[6];
  packet->status = report[7];
  packet->payload = &report[EQ_PROTOCOL_HEADER_SIZE];
  return true;
}

void eq_protocol_response_init(uint8_t report[EQ_PROTOCOL_REPORT_SIZE], uint8_t opcode, uint16_t request_id,
                               eq_protocol_status_t status, uint8_t payload_length) {
  memset(report, 0, EQ_PROTOCOL_REPORT_SIZE);
  report[0] = EQ_MAGIC_0;
  report[1] = EQ_MAGIC_1;
  report[2] = EQ_PROTOCOL_VERSION;
  report[3] = opcode | EQ_OPCODE_RESPONSE;
  eq_protocol_write_u16(&report[4], request_id);
  report[6] = payload_length;
  report[7] = (uint8_t)status;
}

static int32_t to_milli(float value) {
  return (int32_t)lrintf(value * 1000.0f);
}

void eq_protocol_encode_global(uint8_t payload[EQ_PROTOCOL_GLOBAL_PAYLOAD_SIZE], eq_config_t const *config) {
  memset(payload, 0, EQ_PROTOCOL_GLOBAL_PAYLOAD_SIZE);
  payload[0] = config->enabled ? 1u : 0u;
  eq_protocol_write_i32(&payload[4], to_milli(config->preamp_db));
}

bool eq_protocol_decode_global(uint8_t const *payload, size_t length, eq_config_t *config) {
  if (payload == NULL || config == NULL || length != EQ_PROTOCOL_GLOBAL_PAYLOAD_SIZE || payload[0] > 1u) {
    return false;
  }
  config->enabled = payload[0] != 0u;
  config->preamp_db = (float)eq_protocol_read_i32(&payload[4]) / 1000.0f;
  return eq_config_validate(config);
}

void eq_protocol_encode_band(uint8_t payload[EQ_PROTOCOL_BAND_PAYLOAD_SIZE], uint8_t index,
                             eq_filter_config_t const *filter) {
  memset(payload, 0, EQ_PROTOCOL_BAND_PAYLOAD_SIZE);
  payload[0] = index;
  payload[1] = filter->enabled ? 1u : 0u;
  payload[2] = (uint8_t)filter->type;
  payload[3] = (uint8_t)filter->width_mode;
  eq_protocol_write_i32(&payload[5], to_milli(filter->frequency_hz));
  eq_protocol_write_i32(&payload[9], to_milli(filter->gain_db));
  eq_protocol_write_i32(&payload[13], to_milli(filter->q));
  eq_protocol_write_i32(&payload[17], to_milli(filter->bw_octaves));
}

bool eq_protocol_decode_band(uint8_t const *payload, size_t length, uint8_t *index, eq_filter_config_t *filter) {
  if (payload == NULL || index == NULL || filter == NULL || length != EQ_PROTOCOL_BAND_PAYLOAD_SIZE ||
      payload[1] > 1u) {
    return false;
  }
  *index = payload[0];
  filter->enabled = payload[1] != 0u;
  filter->type = (eq_filter_type_t)payload[2];
  filter->width_mode = (eq_width_mode_t)payload[3];
  filter->frequency_hz = (float)eq_protocol_read_i32(&payload[5]) / 1000.0f;
  filter->gain_db = (float)eq_protocol_read_i32(&payload[9]) / 1000.0f;
  filter->q = (float)eq_protocol_read_i32(&payload[13]) / 1000.0f;
  filter->bw_octaves = (float)eq_protocol_read_i32(&payload[17]) / 1000.0f;
  return true;
}
