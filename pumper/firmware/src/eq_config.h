#ifndef EQ_CONFIG_H_
#define EQ_CONFIG_H_

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  EQ_FILTER_LOW_SHELF = 0,
  EQ_FILTER_PEAKING = 1,
  EQ_FILTER_HIGH_SHELF = 2,
} eq_filter_type_t;

typedef enum {
  EQ_WIDTH_Q = 0,
  EQ_WIDTH_BANDWIDTH = 1,
} eq_width_mode_t;

typedef struct {
  bool enabled;
  eq_filter_type_t type;
  eq_width_mode_t width_mode;
  float frequency_hz;
  float q;
  float bw_octaves;
  float gain_db;
} eq_filter_config_t;

#define EQ_NUM_FILTERS 10u

typedef struct {
  bool enabled;
  float preamp_db;
  eq_filter_config_t filters[EQ_NUM_FILTERS];
} eq_config_t;

#define EQ_FREQUENCY_MIN_HZ 20.0f
#define EQ_FREQUENCY_MAX_HZ 20000.0f
#define EQ_GAIN_MIN_DB (-24.0f)
#define EQ_GAIN_MAX_DB 24.0f
#define EQ_PREAMP_MIN_DB (-241.0f)
#define EQ_PREAMP_MAX_DB 12.0f
#define EQ_Q_MIN 0.1f
#define EQ_Q_MAX 20.0f
#define EQ_BANDWIDTH_MIN_OCTAVES 0.1f
#define EQ_BANDWIDTH_MAX_OCTAVES 4.0f
#define EQ_SHELF_SLOPE_MIN 0.1f
#define EQ_SHELF_SLOPE_MAX 1.0f

extern eq_config_t const k_eq_default_config;

void eq_config_set_defaults(eq_config_t *config);
bool eq_config_validate(eq_config_t const *config);
bool eq_config_equal(eq_config_t const *left, eq_config_t const *right);

#endif
