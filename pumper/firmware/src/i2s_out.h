#ifndef I2S_OUT_H_
#define I2S_OUT_H_

#include <stddef.h>
#include <stdint.h>

void i2s_out_init(uint32_t sample_rate_hz);
size_t i2s_out_write_stereo16(const int16_t *interleaved, size_t frame_count);
void i2s_out_set_sample_rate(uint32_t sample_rate_hz);
size_t i2s_out_free_frames(void);

#endif
