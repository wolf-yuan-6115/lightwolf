#ifndef EQ_DSP_H_
#define EQ_DSP_H_

#include <stddef.h>
#include <stdint.h>

#include "eq_config.h"

typedef struct {
  uint16_t left_peak;
  uint16_t right_peak;
  uint64_t left_square_sum;
  uint64_t right_square_sum;
} eq_level_metrics_t;

typedef struct {
  eq_level_metrics_t pre_eq;
  eq_level_metrics_t post_eq;
} eq_block_metrics_t;

void eq_init(uint32_t sample_rate_hz, eq_config_t const *config);
void eq_set_sample_rate(uint32_t sample_rate_hz);
bool eq_set_config(eq_config_t const *config);
void eq_process_interleaved_stereo16(int16_t *interleaved, size_t frame_count,
                                     eq_block_metrics_t *metrics, bool measure_rms);

#endif
