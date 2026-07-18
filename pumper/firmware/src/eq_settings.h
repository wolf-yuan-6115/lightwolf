#ifndef EQ_SETTINGS_H_
#define EQ_SETTINGS_H_

#include <stdbool.h>
#include <stdint.h>

#include "eq_config.h"

#define EQ_PROFILE_COUNT 10u

typedef struct {
  uint16_t present_mask;
  uint8_t default_profile;
  uint32_t bank_generation;
} eq_profile_state_t;

void eq_settings_core_init(void);
bool eq_settings_load(eq_config_t *config, uint32_t *generation);
void eq_settings_get_profile_state(eq_profile_state_t *state);
bool eq_settings_load_profile(uint8_t index, eq_config_t *config, uint32_t *generation);
bool eq_settings_save_profile(uint8_t index, eq_config_t const *config, uint32_t generation);
bool eq_settings_set_default_profile(uint8_t index);
bool eq_settings_delete_profile(uint8_t index);

#endif
