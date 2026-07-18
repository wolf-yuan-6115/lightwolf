#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "eq_config.h"
#include "eq_protocol.h"
#include "eq_settings.h"
#include "hardware/flash.h"
#include "pico/flash.h"

#define TEST_FLASH_SIZE 2097152u
#define PROFILE_STORAGE_OFFSET (TEST_FLASH_SIZE - 2u * FLASH_SECTOR_SIZE)
#define LEGACY_STORAGE_OFFSET (TEST_FLASH_SIZE - FLASH_SECTOR_SIZE)
#define SERIALIZED_CONFIG_SIZE (EQ_PROTOCOL_GLOBAL_PAYLOAD_SIZE + EQ_NUM_FILTERS * 20u)

uint8_t g_fake_flash[TEST_FLASH_SIZE];
uint8_t __flash_binary_end;

void flash_range_erase(uint32_t offset, size_t count) {
  assert(offset + count <= sizeof(g_fake_flash));
  memset(g_fake_flash + offset, 0xff, count);
}

void flash_range_program(uint32_t offset, uint8_t const *data, size_t count) {
  assert(offset + count <= sizeof(g_fake_flash));
  for (size_t i = 0; i < count; i++) g_fake_flash[offset + i] &= data[i];
}

int flash_safe_execute_core_init(void) {
  return PICO_OK;
}

int flash_safe_execute(void (*callback)(void *), void *param, uint32_t timeout_ms) {
  (void)timeout_ms;
  callback(param);
  return PICO_OK;
}

static uint32_t test_crc32(uint8_t const *data, size_t length) {
  uint32_t crc = 0xffffffffu;
  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8u; bit++) {
      crc = (crc >> 1u) ^ (0xedb88320u & (uint32_t)-(int32_t)(crc & 1u));
    }
  }
  return ~crc;
}

static void write_legacy_profile(eq_config_t const *config, uint32_t generation) {
  uint8_t *record = g_fake_flash + LEGACY_STORAGE_OFFSET;
  eq_protocol_write_u32(record, 0x31514550u);
  eq_protocol_write_u16(record + 4u, 1u);
  eq_protocol_write_u16(record + 6u, SERIALIZED_CONFIG_SIZE);
  eq_protocol_write_u32(record + 8u, generation);
  uint8_t *payload = record + 16u;
  eq_protocol_encode_global(payload, config);
  for (uint8_t index = 0u; index < EQ_NUM_FILTERS; index++) {
    uint8_t encoded[EQ_PROTOCOL_BAND_PAYLOAD_SIZE];
    eq_protocol_encode_band(encoded, index, &config->filters[index]);
    memcpy(payload + EQ_PROTOCOL_GLOBAL_PAYLOAD_SIZE + (size_t)index * 20u, encoded + 1u, 20u);
  }
  eq_protocol_write_u32(record + 12u, test_crc32(payload, SERIALIZED_CONFIG_SIZE));
}

static void test_alternating_profile_banks(void) {
  memset(g_fake_flash, 0xff, sizeof(g_fake_flash));
  eq_config_t loaded;
  uint32_t generation;
  assert(!eq_settings_load(&loaded, &generation));

  eq_config_t profile_three = k_eq_default_config;
  profile_three.preamp_db = -3.0f;
  assert(eq_settings_save_profile(3u, &profile_three, 7u));

  eq_profile_state_t state;
  eq_settings_get_profile_state(&state);
  assert(state.default_profile == 3u);
  assert(state.present_mask == (1u << 3u));
  assert(state.bank_generation == 1u);

  eq_config_t profile_seven = k_eq_default_config;
  profile_seven.preamp_db = -7.0f;
  assert(eq_settings_save_profile(7u, &profile_seven, 8u));
  eq_settings_get_profile_state(&state);
  assert(state.default_profile == 3u);
  assert(state.present_mask == ((1u << 3u) | (1u << 7u)));
  assert(state.bank_generation == 2u);
  assert(eq_settings_load_profile(3u, &loaded, &generation));
  assert(eq_config_equal(&loaded, &profile_three));
  assert(generation == 7u);

  assert(eq_settings_set_default_profile(7u));
  eq_settings_get_profile_state(&state);
  assert(state.default_profile == 7u);
  assert(state.bank_generation == 3u);

  // The default update committed bank 0. Corrupt its header to simulate power loss
  // before commit; reloading must recover the complete previous bank.
  g_fake_flash[PROFILE_STORAGE_OFFSET] = 0u;
  assert(eq_settings_load(&loaded, &generation));
  assert(eq_config_equal(&loaded, &profile_three));
  assert(generation == 7u);
  eq_settings_get_profile_state(&state);
  assert(state.default_profile == 3u);
  assert(state.present_mask == ((1u << 3u) | (1u << 7u)));
  assert(state.bank_generation == 2u);
}

static void test_legacy_profile_migration(void) {
  memset(g_fake_flash, 0xff, sizeof(g_fake_flash));
  eq_config_t legacy = k_eq_default_config;
  legacy.preamp_db = -9.5f;
  write_legacy_profile(&legacy, 23u);

  eq_config_t loaded;
  uint32_t generation;
  assert(eq_settings_load(&loaded, &generation));
  assert(eq_config_equal(&loaded, &legacy));
  assert(generation == 23u);

  eq_profile_state_t state;
  eq_settings_get_profile_state(&state);
  assert(state.default_profile == 0u);
  assert(state.present_mask == 1u);
  assert(state.bank_generation == 0u);

  assert(eq_settings_save_profile(0u, &loaded, generation));
  assert(eq_settings_load(&loaded, &generation));
  assert(eq_config_equal(&loaded, &legacy));
  eq_settings_get_profile_state(&state);
  assert(state.present_mask == 1u);
  assert(state.bank_generation == 1u);
}

static void test_profile_deletion(void) {
  memset(g_fake_flash, 0xff, sizeof(g_fake_flash));
  eq_config_t loaded;
  uint32_t generation;
  assert(!eq_settings_load(&loaded, &generation));

  assert(eq_settings_save_profile(3u, &k_eq_default_config, 1u));
  assert(eq_settings_save_profile(7u, &k_eq_default_config, 2u));
  assert(eq_settings_set_default_profile(7u));
  assert(eq_settings_delete_profile(7u));

  eq_profile_state_t state;
  eq_settings_get_profile_state(&state);
  assert(state.present_mask == (1u << 3u));
  assert(state.default_profile == 3u);
  assert(state.bank_generation == 4u);
  assert(!eq_settings_delete_profile(7u));

  assert(eq_settings_delete_profile(3u));
  eq_settings_get_profile_state(&state);
  assert(state.present_mask == 0u);
  assert(state.default_profile == 0u);
  assert(state.bank_generation == 5u);
  assert(!eq_settings_load(&loaded, &generation));
  eq_settings_get_profile_state(&state);
  assert(state.present_mask == 0u);
  assert(state.bank_generation == 5u);
}

int main(void) {
  test_alternating_profile_banks();
  test_legacy_profile_migration();
  test_profile_deletion();
  return 0;
}
