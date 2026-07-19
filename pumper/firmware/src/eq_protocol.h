#ifndef EQ_PROTOCOL_H_
#define EQ_PROTOCOL_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "eq_config.h"

#define EQ_PROTOCOL_REPORT_SIZE 64u
#define EQ_PROTOCOL_HEADER_SIZE 8u
#define EQ_PROTOCOL_PAYLOAD_SIZE (EQ_PROTOCOL_REPORT_SIZE - EQ_PROTOCOL_HEADER_SIZE)
#define EQ_PROTOCOL_VERSION 1u

#define EQ_PROTOCOL_GLOBAL_PAYLOAD_SIZE 8u
#define EQ_PROTOCOL_BAND_PAYLOAD_SIZE 21u
#define EQ_PROTOCOL_STATUS_PAYLOAD_SIZE 32u
#define EQ_PROTOCOL_METER_CONFIG_PAYLOAD_SIZE 4u
#define EQ_PROTOCOL_METER_LEVEL_PAYLOAD_SIZE 28u
#define EQ_PROTOCOL_PROFILE_STATE_PAYLOAD_SIZE 12u
#define EQ_PROTOCOL_PROFILE_RESULT_PAYLOAD_SIZE 8u

typedef enum {
  EQ_OPCODE_HELLO = 0x01,
  EQ_OPCODE_GET_STATUS = 0x02,
  EQ_OPCODE_GET_GLOBAL = 0x03,
  EQ_OPCODE_GET_BAND = 0x04,
  EQ_OPCODE_GET_PROFILES = 0x05,
  EQ_OPCODE_SET_GLOBAL = 0x10,
  EQ_OPCODE_SET_BAND = 0x11,
  EQ_OPCODE_WRITE_FLASH = 0x20,
  EQ_OPCODE_RESTORE_DEFAULTS = 0x21,
  EQ_OPCODE_LOAD_PROFILE = 0x22,
  EQ_OPCODE_SAVE_PROFILE = 0x23,
  EQ_OPCODE_SET_DEFAULT_PROFILE = 0x24,
  EQ_OPCODE_DELETE_PROFILE = 0x25,
  EQ_OPCODE_METER_START = 0x30,
  EQ_OPCODE_METER_KEEPALIVE = 0x31,
  EQ_OPCODE_METER_STOP = 0x32,
  EQ_OPCODE_METER_LEVEL = 0x33,
  EQ_OPCODE_RESTART_DEVICE = 0x40,
  EQ_OPCODE_ENTER_BOOTSEL = 0x41,
} eq_protocol_opcode_t;

#define EQ_OPCODE_RESPONSE 0x80u

typedef enum {
  EQ_STATUS_OK = 0,
  EQ_STATUS_INVALID_PACKET = 1,
  EQ_STATUS_INVALID_COMMAND = 2,
  EQ_STATUS_INVALID_LENGTH = 3,
  EQ_STATUS_INVALID_INDEX = 4,
  EQ_STATUS_OUT_OF_RANGE = 5,
  EQ_STATUS_BUSY = 6,
  EQ_STATUS_STORAGE_ERROR = 7,
} eq_protocol_status_t;

typedef struct {
  uint8_t opcode;
  uint16_t request_id;
  uint8_t payload_length;
  uint8_t status;
  uint8_t const *payload;
} eq_protocol_packet_t;

uint16_t eq_protocol_read_u16(uint8_t const *data);
uint32_t eq_protocol_read_u32(uint8_t const *data);
int32_t eq_protocol_read_i32(uint8_t const *data);
void eq_protocol_write_u16(uint8_t *data, uint16_t value);
void eq_protocol_write_u32(uint8_t *data, uint32_t value);
void eq_protocol_write_i32(uint8_t *data, int32_t value);

bool eq_protocol_decode(uint8_t const *report, size_t length, eq_protocol_packet_t *packet);
void eq_protocol_response_init(uint8_t report[EQ_PROTOCOL_REPORT_SIZE], uint8_t opcode, uint16_t request_id,
                               eq_protocol_status_t status, uint8_t payload_length);

void eq_protocol_encode_global(uint8_t payload[EQ_PROTOCOL_GLOBAL_PAYLOAD_SIZE], eq_config_t const *config);
bool eq_protocol_decode_global(uint8_t const *payload, size_t length, eq_config_t *config);
void eq_protocol_encode_band(uint8_t payload[EQ_PROTOCOL_BAND_PAYLOAD_SIZE], uint8_t index,
                             eq_filter_config_t const *filter);
bool eq_protocol_decode_band(uint8_t const *payload, size_t length, uint8_t *index, eq_filter_config_t *filter);

#endif
