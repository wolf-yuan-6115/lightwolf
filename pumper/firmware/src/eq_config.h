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
  float bw_octaves;
  float gain_db;
} eq_filter_config_t;

#define EQ_NUM_FILTERS 10u
#define EQ_ENABLED true

// Overall output trim before the EQ filters.
#define EQ_PREAMP_DB (-5.3f)

// 10-band parametric EQ filter chain.
static const eq_filter_config_t k_eq_filters[EQ_NUM_FILTERS] = {
    // Filter               Freq.     Q. fact BW     Gain
    {EQ_FILTER_PEAKING,     68.0f,    0.71f,  1.89f, -4.9f},
    {EQ_FILTER_LOW_SHELF,   105.0f,   0.71f,  1.89f, 5.5f},
    {EQ_FILTER_PEAKING,     260.0f,   4.0f,   0.36f, -1.0f},
    {EQ_FILTER_PEAKING,     1300.0f,  3.0f,   0.48f, 1.3f},
    {EQ_FILTER_PEAKING,     1650.0f,  3.0f,   0.48f, -2.7f},
    {EQ_FILTER_PEAKING,     2600.0f,  5.0f,   0.29f, -1.0f},
    {EQ_FILTER_HIGH_SHELF,  3000.0f,  0.35f,  3.33f, 1.0f},
    {EQ_FILTER_PEAKING,     3000.0f,  1.4f,   1.01f, 2.0f},
    {EQ_FILTER_PEAKING,     5100.0f,  4.5f,   0.32f, -4.0f},
    {EQ_FILTER_HIGH_SHELF,  10000.0f, 0.71f,  1.89f, -1.0f},
};

#endif
