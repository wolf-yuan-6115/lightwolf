#include "audio_format.h"

#include <math.h>

size_t audio_format_frame_bytes(audio_sample_format_t format) {
  return format == AUDIO_FORMAT_PCM24 ? 6u : format == AUDIO_FORMAT_PCM16 ? 4u : 0u;
}

bool audio_format_rate_valid(audio_sample_format_t format, uint32_t rate_hz) {
  switch (rate_hz) {
    case 44100u:
    case 48000u:
    case 88200u:
    case 96000u:
      return format == AUDIO_FORMAT_PCM16 || format == AUDIO_FORMAT_PCM24;
    case 176400u:
    case 192000u:
      return format == AUDIO_FORMAT_PCM16;
    default:
      return false;
  }
}

static int32_t read_s24(uint8_t const *p) {
  uint32_t value = (uint32_t)p[0] | (uint32_t)p[1] << 8u | (uint32_t)p[2] << 16u;
  if (value & 0x800000u) value |= 0xff000000u;
  return (int32_t)value;
}

size_t audio_format_decode(float *restrict output, size_t output_frames,
                           uint8_t const *restrict input, size_t input_bytes,
                           audio_sample_format_t format) {
  size_t frame_bytes = audio_format_frame_bytes(format);
  if (!output || !input || !frame_bytes || input_bytes % frame_bytes) return 0u;
  size_t frames = input_bytes / frame_bytes;
  if (frames > output_frames) return 0u;
  for (size_t frame = 0; frame < frames; frame++) {
    if (format == AUDIO_FORMAT_PCM16) {
      size_t offset = frame * 4u;
      output[frame * 2u] = (float)(int16_t)((uint16_t)input[offset] |
                                           (uint16_t)input[offset + 1u] << 8u);
      output[frame * 2u + 1u] = (float)(int16_t)((uint16_t)input[offset + 2u] |
                                                (uint16_t)input[offset + 3u] << 8u);
    } else {
      size_t offset = frame * 6u;
      output[frame * 2u] = (float)read_s24(input + offset) / 256.0f;
      output[frame * 2u + 1u] = (float)read_s24(input + offset + 3u) / 256.0f;
    }
  }
  return frames;
}

static int32_t quantize(float sample, float scale, int32_t minimum, int32_t maximum) {
  float scaled = sample * scale;
  if (scaled >= (float)maximum) return maximum;
  if (scaled <= (float)minimum) return minimum;
  return (int32_t)(scaled < 0.0f ? scaled - 0.5f : scaled + 0.5f);
}

size_t audio_format_pack_i2s(uint32_t *output, float const *input,
                             size_t frames, audio_sample_format_t format) {
  if (!output || !input) return 0u;
  // The board routes the DAC's analog channels crossed, so compensate only at the I2S boundary.
  if (format == AUDIO_FORMAT_PCM16) {
    for (size_t frame = 0; frame < frames; frame++) {
      int32_t left = quantize(input[frame * 2u], 1.0f, -32768, 32767);
      int32_t right = quantize(input[frame * 2u + 1u], 1.0f, -32768, 32767);
      output[frame] = (uint16_t)right | (uint32_t)(uint16_t)left << 16u;
    }
    return frames;
  }
  if (format == AUDIO_FORMAT_PCM24) {
    for (size_t frame = 0; frame < frames; frame++) {
      int32_t left = quantize(input[frame * 2u], 256.0f, -8388608, 8388607);
      int32_t right = quantize(input[frame * 2u + 1u], 256.0f, -8388608, 8388607);
      output[frame * 2u] = ((uint32_t)left & 0x00ffffffu) << 8u;
      output[frame * 2u + 1u] = ((uint32_t)right & 0x00ffffffu) << 8u;
    }
    return frames * 2u;
  }
  return 0u;
}
