#include "audio_format.h"

#include <math.h>
#include <string.h>

#define I2S_OUTPUT_GAIN 0.75
#define PCM16_OUTPUT_MIN (-24576)
#define PCM16_OUTPUT_MAX 24575
#define PCM24_OUTPUT_MIN (-6291456)
#define PCM24_OUTPUT_MAX 6291455
#define PCM32_OUTPUT_MIN (-1610612736)
#define PCM32_OUTPUT_MAX 1610612735

size_t audio_format_frame_bytes(audio_sample_format_t format) {
  return format == AUDIO_FORMAT_PCM32 ? 8u : format == AUDIO_FORMAT_PCM24 ? 6u :
         format == AUDIO_FORMAT_PCM16 ? 4u : 0u;
}

uint8_t audio_format_bit_depth(audio_sample_format_t format) {
  return (uint8_t)(audio_format_frame_bytes(format) * 4u);
}

bool audio_format_rate_valid(audio_sample_format_t format, uint32_t rate_hz) {
  switch (rate_hz) {
    case 44100u:
    case 48000u:
    case 88200u:
    case 96000u:
      return format == AUDIO_FORMAT_PCM16 || format == AUDIO_FORMAT_PCM24 ||
             format == AUDIO_FORMAT_PCM32;
    case 176400u:
    case 192000u:
      return format == AUDIO_FORMAT_PCM16;
    default:
      return false;
  }
}

bool audio_format_rate_change_valid(audio_sample_format_t format, uint32_t rate_hz,
                                    bool streaming) {
  return audio_format_rate_valid(AUDIO_FORMAT_PCM16, rate_hz) &&
         (!streaming || audio_format_rate_valid(format, rate_hz));
}

static int32_t read_s24(uint8_t const *p) {
  uint32_t value = (uint32_t)p[0] | (uint32_t)p[1] << 8u | (uint32_t)p[2] << 16u;
  if (value & 0x800000u) value |= 0xff000000u;
  return (int32_t)value;
}

static int32_t read_s32(uint8_t const *p) {
  uint32_t value = (uint32_t)p[0] | (uint32_t)p[1] << 8u |
                   (uint32_t)p[2] << 16u | (uint32_t)p[3] << 24u;
  // Avoid an out-of-range unsigned-to-signed conversion for negative PCM.
  return value <= INT32_MAX ? (int32_t)value : -1 - (int32_t)(UINT32_MAX - value);
}

size_t audio_format_decode(double *restrict output, size_t output_frames,
                           uint8_t const *restrict input, size_t input_bytes,
                           audio_sample_format_t format) {
  size_t frame_bytes = audio_format_frame_bytes(format);
  if (!output || !input || !frame_bytes || input_bytes % frame_bytes) return 0u;
  size_t frames = input_bytes / frame_bytes;
  if (frames > output_frames) return 0u;
  for (size_t frame = 0; frame < frames; frame++) {
    if (format == AUDIO_FORMAT_PCM16) {
      size_t offset = frame * 4u;
      output[frame * 2u] = (double)(int16_t)((uint16_t)input[offset] |
                                           (uint16_t)input[offset + 1u] << 8u);
      output[frame * 2u + 1u] = (double)(int16_t)((uint16_t)input[offset + 2u] |
                                                (uint16_t)input[offset + 3u] << 8u);
    } else if (format == AUDIO_FORMAT_PCM24) {
      size_t offset = frame * 6u;
      output[frame * 2u] = (double)read_s24(input + offset) / 256.0;
      output[frame * 2u + 1u] = (double)read_s24(input + offset + 3u) / 256.0;
    } else {
      size_t offset = frame * 8u;
      output[frame * 2u] = (double)read_s32(input + offset) / 65536.0;
      output[frame * 2u + 1u] = (double)read_s32(input + offset + 4u) / 65536.0;
    }
  }
  return frames;
}

static int32_t quantize(double sample, double scale, int32_t minimum, int32_t maximum) {
  double scaled = sample * scale;
  if (isnan(scaled)) return 0;
  if (scaled >= (double)maximum) return maximum;
  if (scaled <= (double)minimum) return minimum;
  return (int32_t)(scaled < 0.0 ? scaled - 0.5 : scaled + 0.5);
}

static void write_word(unsigned char *output, size_t index, uint32_t word) {
  memcpy(output + index * sizeof(word), &word, sizeof(word));
}

size_t audio_format_pack_i2s(uint32_t *output, double const *input,
                             size_t frames, audio_sample_format_t format) {
  if (!output || !input) return 0u;
  unsigned char *packed = (unsigned char *)output;
  // Byte writes keep overlapping double input and packed output alias-safe.
  // The board routes the DAC's analog channels crossed, so compensate only at the I2S boundary.
  if (format == AUDIO_FORMAT_PCM16) {
    for (size_t frame = 0; frame < frames; frame++) {
      int32_t left = quantize(input[frame * 2u], I2S_OUTPUT_GAIN,
                              PCM16_OUTPUT_MIN, PCM16_OUTPUT_MAX);
      int32_t right = quantize(input[frame * 2u + 1u], I2S_OUTPUT_GAIN,
                               PCM16_OUTPUT_MIN, PCM16_OUTPUT_MAX);
      write_word(packed, frame, (uint16_t)right | (uint32_t)(uint16_t)left << 16u);
    }
    return frames;
  }
  if (format == AUDIO_FORMAT_PCM24) {
    for (size_t frame = 0; frame < frames; frame++) {
      int32_t left = quantize(input[frame * 2u], 256.0 * I2S_OUTPUT_GAIN,
                              PCM24_OUTPUT_MIN, PCM24_OUTPUT_MAX);
      int32_t right = quantize(input[frame * 2u + 1u], 256.0 * I2S_OUTPUT_GAIN,
                               PCM24_OUTPUT_MIN, PCM24_OUTPUT_MAX);
      write_word(packed, frame * 2u, ((uint32_t)left & 0x00ffffffu) << 8u);
      write_word(packed, frame * 2u + 1u, ((uint32_t)right & 0x00ffffffu) << 8u);
    }
    return frames * 2u;
  }
  if (format == AUDIO_FORMAT_PCM32) {
    for (size_t frame = 0; frame < frames; frame++) {
      int32_t left = quantize(input[frame * 2u], 65536.0 * I2S_OUTPUT_GAIN,
                              PCM32_OUTPUT_MIN, PCM32_OUTPUT_MAX);
      int32_t right = quantize(input[frame * 2u + 1u], 65536.0 * I2S_OUTPUT_GAIN,
                               PCM32_OUTPUT_MIN, PCM32_OUTPUT_MAX);
      write_word(packed, frame * 2u, (uint32_t)left);
      write_word(packed, frame * 2u + 1u, (uint32_t)right);
    }
    return frames * 2u;
  }
  return 0u;
}
