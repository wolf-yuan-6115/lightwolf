#ifndef EQ_CONFIG_H_
#define EQ_CONFIG_H_

#include <stdbool.h>

typedef enum {
  EQ_FILTER_LOW_SHELF = 0,
  EQ_FILTER_PEAKING = 1,
  EQ_FILTER_HIGH_SHELF = 2,
} eq_filter_type_t;

typedef struct {
  eq_filter_type_t type;
  float frequency_hz;
  float q;
  float gain_db;
} eq_filter_config_t;

#define EQ_NUM_FILTERS 6u
#define EQ_ENABLED true

// Overall output trim before the EQ filters.
#define EQ_PREAMP_DB (-8.8f)

// 6-band parametric EQ filter chain.
static const eq_filter_config_t k_eq_filters[EQ_NUM_FILTERS] = {
    {EQ_FILTER_LOW_SHELF, 105.0f, 0.70f, 11.7f},
    {EQ_FILTER_PEAKING, 58.0f, 0.66f, -10.7f},
    {EQ_FILTER_PEAKING, 3048.0f, 3.93f, 3.4f},
    {EQ_FILTER_PEAKING, 5099.0f, 5.77f, -4.8f},
    {EQ_FILTER_PEAKING, 9982.0f, 1.35f, 2.6f},
    {EQ_FILTER_HIGH_SHELF, 10000.0f, 0.70f, -0.6f},
};

#endif
