#ifndef AUDIO_FORMAT_H_
#define AUDIO_FORMAT_H_

#include <float.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

_Static_assert(sizeof(double) == 8 && DBL_MANT_DIG == 53 && FLT_RADIX == 2,
               "PCM32 quantization requires binary64 double precision");

typedef enum {
  // Values match the USB AudioStreaming alternate settings.
  AUDIO_FORMAT_PCM16 = 1,
  AUDIO_FORMAT_PCM24 = 2,
  AUDIO_FORMAT_PCM32 = 3,
} audio_sample_format_t;

size_t audio_format_frame_bytes(audio_sample_format_t format);
uint8_t audio_format_bit_depth(audio_sample_format_t format);
bool audio_format_rate_valid(audio_sample_format_t format, uint32_t rate_hz);
// With alternate 0 selected, the shared clock may be set before a new PCM
// alternate. Only an active stream constrains the clock to its current format.
bool audio_format_rate_change_valid(audio_sample_format_t format, uint32_t rate_hz,
                                    bool streaming);
size_t audio_format_decode(float *restrict output, size_t output_frames,
                           uint8_t const *restrict input, size_t input_bytes,
                           audio_sample_format_t format);
// Output may start at the same address as input; packing consumes each pair
// before overwriting its bytes. Other partial overlaps are not supported.
size_t audio_format_pack_i2s(uint32_t *output, float const *input,
                             size_t frames, audio_sample_format_t format);

#endif
