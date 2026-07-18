#ifndef EQ_DSP_H_
#define EQ_DSP_H_

#include <stddef.h>
#include <stdint.h>

#include "eq_config.h"

void eq_init(uint32_t sample_rate_hz, eq_config_t const *config);
void eq_set_sample_rate(uint32_t sample_rate_hz);
bool eq_set_config(eq_config_t const *config);
void eq_process_interleaved_stereo16(int16_t *interleaved, size_t frame_count);

#endif
