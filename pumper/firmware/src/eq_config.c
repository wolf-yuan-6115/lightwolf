#include "eq_config.h"

#include <math.h>
#include <string.h>

eq_config_t const k_eq_default_config = {
    .enabled = true,
    .preamp_db = 0.0f,
    .filters = {
        {true, EQ_FILTER_PEAKING, EQ_WIDTH_BANDWIDTH, 68.0f, 0.71f, 1.89f, 0.0f},
        {true, EQ_FILTER_LOW_SHELF, EQ_WIDTH_Q, 105.0f, 0.71f, 1.89f, 0.0f},
        {true, EQ_FILTER_PEAKING, EQ_WIDTH_BANDWIDTH, 260.0f, 4.0f, 0.36f, 0.0f},
        {true, EQ_FILTER_PEAKING, EQ_WIDTH_BANDWIDTH, 1300.0f, 3.0f, 0.48f, 0.0f},
        {true, EQ_FILTER_PEAKING, EQ_WIDTH_BANDWIDTH, 1650.0f, 3.0f, 0.48f, 0.0f},
        {true, EQ_FILTER_PEAKING, EQ_WIDTH_BANDWIDTH, 2600.0f, 5.0f, 0.29f, 0.0f},
        {true, EQ_FILTER_HIGH_SHELF, EQ_WIDTH_Q, 3000.0f, 0.35f, 3.33f, 0.0f},
        {true, EQ_FILTER_PEAKING, EQ_WIDTH_BANDWIDTH, 3000.0f, 1.4f, 1.01f, 0.0f},
        {true, EQ_FILTER_PEAKING, EQ_WIDTH_BANDWIDTH, 5100.0f, 4.5f, 0.32f, 0.0f},
        {true, EQ_FILTER_HIGH_SHELF, EQ_WIDTH_Q, 10000.0f, 0.71f, 1.89f, 0.0f},
    },
};

static bool finite_in_range(float value, float minimum, float maximum) {
  return isfinite(value) && value >= minimum && value <= maximum;
}

void eq_config_set_defaults(eq_config_t *config) {
  if (config != NULL) {
    *config = k_eq_default_config;
  }
}

bool eq_config_validate(eq_config_t const *config) {
  if (config == NULL || !finite_in_range(config->preamp_db, EQ_PREAMP_MIN_DB, EQ_PREAMP_MAX_DB)) {
    return false;
  }

  for (uint32_t i = 0; i < EQ_NUM_FILTERS; i++) {
    eq_filter_config_t const *filter = &config->filters[i];
    if ((uint32_t)filter->type > EQ_FILTER_HIGH_SHELF) {
      return false;
    }
    if ((uint32_t)filter->width_mode > EQ_WIDTH_BANDWIDTH) {
      return false;
    }
    if (!finite_in_range(filter->frequency_hz, EQ_FREQUENCY_MIN_HZ, EQ_FREQUENCY_MAX_HZ) ||
        !finite_in_range(filter->gain_db, EQ_GAIN_MIN_DB, EQ_GAIN_MAX_DB) ||
        !finite_in_range(filter->bw_octaves, EQ_BANDWIDTH_MIN_OCTAVES, EQ_BANDWIDTH_MAX_OCTAVES)) {
      return false;
    }
    float q_max = filter->type == EQ_FILTER_PEAKING ? EQ_Q_MAX : EQ_SHELF_SLOPE_MAX;
    if (!finite_in_range(filter->q, EQ_Q_MIN, q_max)) {
      return false;
    }
  }
  return true;
}

bool eq_config_equal(eq_config_t const *left, eq_config_t const *right) {
  if (left == NULL || right == NULL || left->enabled != right->enabled || left->preamp_db != right->preamp_db) {
    return false;
  }
  for (uint32_t i = 0; i < EQ_NUM_FILTERS; i++) {
    eq_filter_config_t const *a = &left->filters[i];
    eq_filter_config_t const *b = &right->filters[i];
    if (a->enabled != b->enabled || a->type != b->type || a->width_mode != b->width_mode ||
        a->frequency_hz != b->frequency_hz || a->q != b->q || a->bw_octaves != b->bw_octaves ||
        a->gain_db != b->gain_db) {
      return false;
    }
  }
  return true;
}
