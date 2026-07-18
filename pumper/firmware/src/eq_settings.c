#include "eq_settings.h"

#include <limits.h>
#include <string.h>

#include "eq_protocol.h"
#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "pico/flash.h"

#define PROFILE_BANK_MAGIC 0x32424c50u
#define PROFILE_RECORD_MAGIC 0x32465250u
#define PROFILE_SCHEMA_VERSION 2u
#define PROFILE_BANK_COUNT 2u
#define PROFILE_CONFIG_OFFSET 32u
#define PROFILE_PRESENT_MARKER 0xa5u
#define PROFILE_BANK_USED_SIZE ((EQ_PROFILE_COUNT + 1u) * FLASH_PAGE_SIZE)
#define PROFILE_STORAGE_SIZE (PROFILE_BANK_COUNT * FLASH_SECTOR_SIZE)
#define PROFILE_STORAGE_OFFSET (PICO_FLASH_SIZE_BYTES - PROFILE_STORAGE_SIZE)

#define LEGACY_MAGIC 0x31514550u
#define LEGACY_SCHEMA_VERSION 1u
#define LEGACY_HEADER_SIZE 16u
#define SETTINGS_CONFIG_SIZE (EQ_PROTOCOL_GLOBAL_PAYLOAD_SIZE + EQ_NUM_FILTERS * 20u)
#define LEGACY_FLASH_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)

_Static_assert(PROFILE_BANK_USED_SIZE <= FLASH_SECTOR_SIZE, "Profile bank exceeds one flash sector");
_Static_assert(PROFILE_CONFIG_OFFSET + SETTINGS_CONFIG_SIZE <= FLASH_PAGE_SIZE,
               "Profile record exceeds one flash page");

extern uint8_t __flash_binary_end;

typedef struct {
  bool present;
  uint32_t generation;
  eq_config_t config;
} profile_slot_t;

typedef struct {
  uint32_t offset;
  uint8_t const *image;
} flash_write_params_t;

typedef struct {
  bool valid;
  uint8_t default_profile;
  uint16_t present_mask;
  uint32_t generation;
} bank_header_t;

static profile_slot_t s_profiles[EQ_PROFILE_COUNT];
static uint8_t s_default_profile = 0u;
static uint32_t s_bank_generation = 0u;
static int8_t s_current_bank = -1;
static uint8_t s_flash_image[FLASH_SECTOR_SIZE];

static uint32_t crc32(uint8_t const *data, size_t length) {
  uint32_t crc = 0xffffffffu;
  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8u; bit++) {
      crc = (crc >> 1u) ^ (0xedb88320u & (uint32_t)-(int32_t)(crc & 1u));
    }
  }
  return ~crc;
}

static uint32_t bank_flash_offset(uint8_t bank) {
  return PROFILE_STORAGE_OFFSET + (uint32_t)bank * FLASH_SECTOR_SIZE;
}

static uint8_t const *flash_pointer(uint32_t offset) {
  return (uint8_t const *)(XIP_BASE + offset);
}

static void encode_config(uint8_t *payload, eq_config_t const *config) {
  eq_protocol_encode_global(payload, config);
  for (uint8_t i = 0; i < EQ_NUM_FILTERS; i++) {
    uint8_t encoded[EQ_PROTOCOL_BAND_PAYLOAD_SIZE];
    eq_protocol_encode_band(encoded, i, &config->filters[i]);
    memcpy(payload + EQ_PROTOCOL_GLOBAL_PAYLOAD_SIZE + (size_t)i * 20u, encoded + 1u, 20u);
  }
}

static bool decode_config(uint8_t const *payload, eq_config_t *config) {
  eq_config_set_defaults(config);
  if (!eq_protocol_decode_global(payload, EQ_PROTOCOL_GLOBAL_PAYLOAD_SIZE, config)) return false;
  for (uint8_t i = 0; i < EQ_NUM_FILTERS; i++) {
    uint8_t encoded[EQ_PROTOCOL_BAND_PAYLOAD_SIZE] = {0};
    encoded[0] = i;
    memcpy(encoded + 1u, payload + EQ_PROTOCOL_GLOBAL_PAYLOAD_SIZE + (size_t)i * 20u, 20u);
    uint8_t decoded_index;
    if (!eq_protocol_decode_band(encoded, sizeof(encoded), &decoded_index, &config->filters[i]) ||
        decoded_index != i) {
      return false;
    }
  }
  return eq_config_validate(config);
}

static bank_header_t read_bank_header(uint8_t bank) {
  bank_header_t result = {0};
  uint8_t const *header = flash_pointer(bank_flash_offset(bank));
  if (eq_protocol_read_u32(header) != PROFILE_BANK_MAGIC ||
      eq_protocol_read_u16(header + 4u) != PROFILE_SCHEMA_VERSION || header[6] != EQ_PROFILE_COUNT ||
      header[7] >= EQ_PROFILE_COUNT || eq_protocol_read_u32(header + 16u) != crc32(header, 16u)) {
    return result;
  }
  result.valid = true;
  result.default_profile = header[7];
  result.generation = eq_protocol_read_u32(header + 8u);
  result.present_mask = eq_protocol_read_u16(header + 12u) & ((1u << EQ_PROFILE_COUNT) - 1u);
  return result;
}

static bool read_profile(uint8_t bank, uint8_t index, profile_slot_t *slot) {
  uint32_t offset = bank_flash_offset(bank) + (uint32_t)(index + 1u) * FLASH_PAGE_SIZE;
  uint8_t const *record = flash_pointer(offset);
  if (eq_protocol_read_u32(record) != PROFILE_RECORD_MAGIC ||
      eq_protocol_read_u16(record + 4u) != PROFILE_SCHEMA_VERSION || record[6] != index ||
      record[7] != PROFILE_PRESENT_MARKER || eq_protocol_read_u16(record + 12u) != SETTINGS_CONFIG_SIZE ||
      eq_protocol_read_u32(record + 16u) != crc32(record + PROFILE_CONFIG_OFFSET, SETTINGS_CONFIG_SIZE)) {
    return false;
  }
  eq_config_t config;
  if (!decode_config(record + PROFILE_CONFIG_OFFSET, &config)) return false;
  slot->present = true;
  slot->generation = eq_protocol_read_u32(record + 8u);
  slot->config = config;
  return true;
}

static bool generation_is_newer(uint32_t candidate, uint32_t current) {
  return (int32_t)(candidate - current) > 0;
}

static bool load_profile_bank(void) {
  bank_header_t headers[PROFILE_BANK_COUNT];
  for (uint8_t bank = 0u; bank < PROFILE_BANK_COUNT; bank++) headers[bank] = read_bank_header(bank);

  int8_t selected = -1;
  if (headers[0].valid) selected = 0;
  if (headers[1].valid && (selected < 0 || generation_is_newer(headers[1].generation, headers[0].generation))) {
    selected = 1;
  }
  if (selected < 0) return false;

  bank_header_t const *header = &headers[(uint8_t)selected];
  memset(s_profiles, 0, sizeof(s_profiles));
  for (uint8_t index = 0u; index < EQ_PROFILE_COUNT; index++) {
    if ((header->present_mask & (1u << index)) != 0u) {
      (void)read_profile((uint8_t)selected, index, &s_profiles[index]);
    }
  }
  s_current_bank = selected;
  s_bank_generation = header->generation;
  s_default_profile = header->default_profile;
  if (!s_profiles[s_default_profile].present) {
    for (uint8_t index = 0u; index < EQ_PROFILE_COUNT; index++) {
      if (s_profiles[index].present) {
        s_default_profile = index;
        break;
      }
    }
  }
  return s_profiles[s_default_profile].present;
}

static bool load_legacy_profile(void) {
  uint8_t const *record = flash_pointer(LEGACY_FLASH_OFFSET);
  if (eq_protocol_read_u32(record) != LEGACY_MAGIC ||
      eq_protocol_read_u16(record + 4u) != LEGACY_SCHEMA_VERSION ||
      eq_protocol_read_u16(record + 6u) != SETTINGS_CONFIG_SIZE ||
      eq_protocol_read_u32(record + 12u) != crc32(record + LEGACY_HEADER_SIZE, SETTINGS_CONFIG_SIZE)) {
    return false;
  }
  eq_config_t config;
  if (!decode_config(record + LEGACY_HEADER_SIZE, &config)) return false;
  s_profiles[0].present = true;
  s_profiles[0].generation = eq_protocol_read_u32(record + 8u);
  s_profiles[0].config = config;
  s_default_profile = 0u;
  return true;
}

static void encode_profile_record(uint8_t index, uint8_t *record) {
  profile_slot_t const *slot = &s_profiles[index];
  if (!slot->present) return;
  eq_protocol_write_u32(record, PROFILE_RECORD_MAGIC);
  eq_protocol_write_u16(record + 4u, PROFILE_SCHEMA_VERSION);
  record[6] = index;
  record[7] = PROFILE_PRESENT_MARKER;
  eq_protocol_write_u32(record + 8u, slot->generation);
  eq_protocol_write_u16(record + 12u, SETTINGS_CONFIG_SIZE);
  encode_config(record + PROFILE_CONFIG_OFFSET, &slot->config);
  eq_protocol_write_u32(record + 16u, crc32(record + PROFILE_CONFIG_OFFSET, SETTINGS_CONFIG_SIZE));
}

static void build_bank_image(uint8_t default_profile, uint32_t bank_generation) {
  memset(s_flash_image, 0xff, sizeof(s_flash_image));
  uint16_t present_mask = 0u;
  for (uint8_t index = 0u; index < EQ_PROFILE_COUNT; index++) {
    if (s_profiles[index].present) {
      present_mask |= (uint16_t)(1u << index);
      encode_profile_record(index, s_flash_image + (uint32_t)(index + 1u) * FLASH_PAGE_SIZE);
    }
  }

  uint8_t *header = s_flash_image;
  eq_protocol_write_u32(header, PROFILE_BANK_MAGIC);
  eq_protocol_write_u16(header + 4u, PROFILE_SCHEMA_VERSION);
  header[6] = EQ_PROFILE_COUNT;
  header[7] = default_profile;
  eq_protocol_write_u32(header + 8u, bank_generation);
  eq_protocol_write_u16(header + 12u, present_mask);
  header[14] = 0u;
  header[15] = 0u;
  eq_protocol_write_u32(header + 16u, crc32(header, 16u));
}

static void flash_write_callback(void *param) {
  flash_write_params_t const *params = (flash_write_params_t const *)param;
  flash_range_erase(params->offset, FLASH_SECTOR_SIZE);
  flash_range_program(params->offset + FLASH_PAGE_SIZE, params->image + FLASH_PAGE_SIZE,
                      EQ_PROFILE_COUNT * FLASH_PAGE_SIZE);
  flash_range_program(params->offset, params->image, FLASH_PAGE_SIZE);
}

void eq_settings_core_init(void) {
  (void)flash_safe_execute_core_init();
}

bool eq_settings_load(eq_config_t *config, uint32_t *generation) {
  if (config == NULL || generation == NULL) return false;
  memset(s_profiles, 0, sizeof(s_profiles));
  s_current_bank = -1;
  s_bank_generation = 0u;
  s_default_profile = 0u;
  if (!load_profile_bank() && !load_legacy_profile()) return false;
  *config = s_profiles[s_default_profile].config;
  *generation = s_profiles[s_default_profile].generation;
  return true;
}

void eq_settings_get_profile_state(eq_profile_state_t *state) {
  if (state == NULL) return;
  state->present_mask = 0u;
  for (uint8_t index = 0u; index < EQ_PROFILE_COUNT; index++) {
    if (s_profiles[index].present) state->present_mask |= (uint16_t)(1u << index);
  }
  state->default_profile = s_default_profile;
  state->bank_generation = s_bank_generation;
}

bool eq_settings_load_profile(uint8_t index, eq_config_t *config, uint32_t *generation) {
  if (index >= EQ_PROFILE_COUNT || config == NULL || generation == NULL || !s_profiles[index].present) return false;
  *config = s_profiles[index].config;
  *generation = s_profiles[index].generation;
  return true;
}

static bool write_profile_bank(uint8_t default_profile) {
  if (default_profile >= EQ_PROFILE_COUNT) return false;
  bool any_profile_present = false;
  for (uint8_t index = 0u; index < EQ_PROFILE_COUNT; index++) {
    if (s_profiles[index].present) {
      any_profile_present = true;
      break;
    }
  }
  if (any_profile_present && !s_profiles[default_profile].present) return false;
  if (!any_profile_present) default_profile = 0u;
#ifndef EQ_SETTINGS_TEST
  uintptr_t binary_end_offset = (uintptr_t)&__flash_binary_end - (uintptr_t)XIP_BASE;
  if (binary_end_offset > PROFILE_STORAGE_OFFSET) return false;
#endif

  uint8_t target_bank = s_current_bank == 0 ? 1u : 0u;
  uint32_t next_bank_generation = s_bank_generation + 1u;
  build_bank_image(default_profile, next_bank_generation);

  flash_write_params_t params = {.offset = bank_flash_offset(target_bank), .image = s_flash_image};
  if (flash_safe_execute(flash_write_callback, &params, UINT_MAX) != PICO_OK ||
      memcmp(flash_pointer(params.offset), s_flash_image, PROFILE_BANK_USED_SIZE) != 0) {
    return false;
  }

  s_current_bank = (int8_t)target_bank;
  s_bank_generation = next_bank_generation;
  s_default_profile = default_profile;
  return true;
}

bool eq_settings_save_profile(uint8_t index, eq_config_t const *config, uint32_t generation) {
  if (index >= EQ_PROFILE_COUNT || !eq_config_validate(config)) return false;
  bool had_default = s_profiles[s_default_profile].present;
  profile_slot_t previous = s_profiles[index];
  s_profiles[index].present = true;
  s_profiles[index].generation = generation;
  s_profiles[index].config = *config;
  uint8_t default_profile = had_default ? s_default_profile : index;
  if (!write_profile_bank(default_profile)) {
    s_profiles[index] = previous;
    return false;
  }
  return true;
}

bool eq_settings_set_default_profile(uint8_t index) {
  if (index >= EQ_PROFILE_COUNT || !s_profiles[index].present) return false;
  return write_profile_bank(index);
}

bool eq_settings_delete_profile(uint8_t index) {
  if (index >= EQ_PROFILE_COUNT || !s_profiles[index].present) return false;

  profile_slot_t previous = s_profiles[index];
  s_profiles[index].present = false;
  uint8_t next_default = s_default_profile;
  if (index == s_default_profile) {
    next_default = 0u;
    for (uint8_t candidate = 0u; candidate < EQ_PROFILE_COUNT; candidate++) {
      if (s_profiles[candidate].present) {
        next_default = candidate;
        break;
      }
    }
  }
  if (!write_profile_bank(next_default)) {
    s_profiles[index] = previous;
    return false;
  }
  return true;
}
