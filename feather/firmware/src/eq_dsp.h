#ifndef EQ_DSP_H_
#define EQ_DSP_H_

#include <stddef.h>
#include <stdint.h>

void eq_init(uint32_t sample_rate_hz);
void eq_set_sample_rate(uint32_t sample_rate_hz);
void eq_process_interleaved_stereo16(int16_t *interleaved, size_t frame_count);

#endif
