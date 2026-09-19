#ifndef AUDIO_FORMAT_H_
#define AUDIO_FORMAT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
  AUDIO_FORMAT_PCM16 = 1,
  AUDIO_FORMAT_PCM24 = 2,
} audio_sample_format_t;

size_t audio_format_frame_bytes(audio_sample_format_t format);
bool audio_format_rate_valid(audio_sample_format_t format, uint32_t rate_hz);
size_t audio_format_decode(float *restrict output, size_t output_frames,
                           uint8_t const *restrict input, size_t input_bytes,
                           audio_sample_format_t format);
size_t audio_format_pack_i2s(uint32_t *output, float const *input,
                             size_t frames, audio_sample_format_t format);

#endif
