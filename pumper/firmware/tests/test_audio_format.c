#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "audio_format.h"

static void test_rate_matrix(void) {
  uint32_t common[] = {44100u, 48000u, 88200u, 96000u};
  for (size_t i = 0; i < sizeof(common) / sizeof(common[0]); ++i) {
    assert(audio_format_rate_valid(AUDIO_FORMAT_PCM16, common[i]));
    assert(audio_format_rate_valid(AUDIO_FORMAT_PCM24, common[i]));
    assert(audio_format_rate_valid(AUDIO_FORMAT_PCM32, common[i]));
    assert(!audio_format_rate_valid((audio_sample_format_t)0, common[i]));
  }
  assert(audio_format_rate_valid(AUDIO_FORMAT_PCM16, 176400u));
  assert(audio_format_rate_valid(AUDIO_FORMAT_PCM16, 192000u));
  assert(!audio_format_rate_valid(AUDIO_FORMAT_PCM24, 176400u));
  assert(!audio_format_rate_valid(AUDIO_FORMAT_PCM24, 192000u));
  assert(!audio_format_rate_valid(AUDIO_FORMAT_PCM32, 176400u));
  assert(!audio_format_rate_valid(AUDIO_FORMAT_PCM32, 192000u));
  assert(!audio_format_rate_valid(AUDIO_FORMAT_PCM32, 48001u));
  assert(!audio_format_rate_valid(AUDIO_FORMAT_PCM16, 48001u));
  assert(audio_format_frame_bytes(AUDIO_FORMAT_PCM32) == 8u);
  assert(audio_format_bit_depth(AUDIO_FORMAT_PCM16) == 16u);
  assert(audio_format_bit_depth(AUDIO_FORMAT_PCM24) == 24u);
  assert(audio_format_bit_depth(AUDIO_FORMAT_PCM32) == 32u);
  assert(audio_format_frame_bytes((audio_sample_format_t)0) == 0u);
  assert(audio_format_bit_depth((audio_sample_format_t)4) == 0u);
}

static void test_idle_clock_negotiation(void) {
  // Linux may retain PCM24 after closing a 96 kHz stream, then set the
  // shared clock to 192 kHz before selecting the PCM16 alternate.
  assert(audio_format_rate_change_valid(AUDIO_FORMAT_PCM24, 192000u, false));
  assert(audio_format_rate_change_valid(AUDIO_FORMAT_PCM24, 176400u, false));
  assert(audio_format_rate_valid(AUDIO_FORMAT_PCM16, 192000u));
  assert(!audio_format_rate_valid(AUDIO_FORMAT_PCM24, 192000u));
  assert(!audio_format_rate_change_valid(AUDIO_FORMAT_PCM24, 192000u, true));
  assert(!audio_format_rate_change_valid(AUDIO_FORMAT_PCM24, 176400u, true));
  assert(audio_format_rate_change_valid(AUDIO_FORMAT_PCM16, 192000u, true));
  assert(audio_format_rate_change_valid(AUDIO_FORMAT_PCM24, 96000u, true));
  assert(!audio_format_rate_change_valid(AUDIO_FORMAT_PCM16, 48001u, false));
  assert(!audio_format_rate_change_valid(AUDIO_FORMAT_PCM24, 384000u, false));
  assert(audio_format_rate_change_valid(AUDIO_FORMAT_PCM32, 192000u, false));
  assert(audio_format_rate_change_valid(AUDIO_FORMAT_PCM32, 176400u, false));
  assert(!audio_format_rate_change_valid(AUDIO_FORMAT_PCM32, 192000u, true));
  assert(!audio_format_rate_change_valid(AUDIO_FORMAT_PCM32, 176400u, true));
  assert(audio_format_rate_change_valid(AUDIO_FORMAT_PCM16, 96000u, false));
  assert(audio_format_rate_change_valid(AUDIO_FORMAT_PCM32, 96000u, true));
  assert(!audio_format_rate_change_valid(AUDIO_FORMAT_PCM32, 384000u, false));
}

static void test_decode(void) {
  uint8_t pcm16[] = {0x00, 0x80, 0xff, 0x7f, 0x34, 0x12, 0xcc, 0xed};
  float samples[4];
  assert(audio_format_decode(samples, 2u, pcm16, sizeof(pcm16), AUDIO_FORMAT_PCM16) == 2u);
  assert(samples[0] == -32768.0 && samples[1] == 32767.0);
  assert(samples[2] == 4660.0 && samples[3] == -4660.0);

  uint8_t pcm24[] = {0x00, 0x00, 0x80, 0xff, 0xff, 0x7f,
                     0x00, 0x01, 0x00, 0x00, 0xff, 0xff};
  assert(audio_format_decode(samples, 2u, pcm24, sizeof(pcm24), AUDIO_FORMAT_PCM24) == 2u);
  assert(samples[0] == -32768.0);
  assert(fabs(samples[1] - (8388607.0 / 256.0)) < 0.01);
  assert(samples[2] == 1.0 && samples[3] == -1.0);
  assert(audio_format_decode(samples, 2u, pcm24, sizeof(pcm24) - 1u, AUDIO_FORMAT_PCM24) == 0u);
  assert(audio_format_decode(samples, 1u, pcm24, sizeof(pcm24), AUDIO_FORMAT_PCM24) == 0u);
}

static void test_pack(void) {
  float samples[] = {-32768.0, 32767.0, -40000.0, 40000.0, 1.4, -1.6};
  uint32_t words[6] = {0};
  assert(audio_format_pack_i2s(words, samples, 3u, AUDIO_FORMAT_PCM16) == 3u);
  assert(words[0] == 0xa0005fffu);
  assert(words[1] == 0xa0005fffu);
  assert(words[2] == 0x0001ffffu);

  float pcm24[] = {-32768.0, 32768.0, -40000.0, 40000.0, 1.0, -1.0};
  assert(audio_format_pack_i2s(words, pcm24, 3u, AUDIO_FORMAT_PCM24) == 6u);
  assert(words[0] == 0xa0000000u);
  assert(words[1] == 0x5fffff00u);
  assert(words[2] == 0xa0000000u);
  assert(words[3] == 0x5fffff00u);
  assert(words[4] == 0x0000c000u);
  assert(words[5] == 0xffff4000u);
}

static void test_in_place_packing(void) {
  float const samples[] = {-32768.0, 32767.0, 12345.123, -23456.456, 1.5, -1.5};
  audio_sample_format_t formats[] = {AUDIO_FORMAT_PCM16, AUDIO_FORMAT_PCM24, AUDIO_FORMAT_PCM32};
  for (unsigned f = 0; f < 3u; ++f) {
    union { float samples[6]; uint32_t words[6]; } block;
    uint32_t expected[6];
    memcpy(block.samples, samples, sizeof(samples));
    size_t count = audio_format_pack_i2s(expected, samples, 3u, formats[f]);
    assert(audio_format_pack_i2s(block.words, block.samples, 3u, formats[f]) == count);
    assert(memcmp(block.words, expected, count * sizeof(uint32_t)) == 0);
  }
}

static void test_pcm32_decode_and_pack(void) {
  int32_t const values[] = {INT32_MIN, INT32_MAX, 16777217, -16777217,
                            1, -1, 2, -2, 0x12345678, -0x12345678};
  uint8_t bytes[sizeof(values)];
  for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
    uint32_t v = (uint32_t)values[i];
    for (unsigned b = 0; b < 4u; ++b) bytes[i * 4u + b] = (uint8_t)(v >> (b * 8u));
  }
  float samples[10];
  assert(audio_format_decode(samples, 5u, bytes, sizeof(bytes), AUDIO_FORMAT_PCM32) == 5u);
  for (unsigned i = 0; i < 10u; ++i) assert(samples[i] == (float)values[i] / 65536.0f);
  // PCM32 transport is retained, but DSP precision is deliberately binary32.
  assert(samples[1] == 32768.0f);
  assert(samples[2] == 256.0f);

  uint32_t words[10];
  assert(audio_format_pack_i2s(words, samples, 5u, AUDIO_FORMAT_PCM32) == 10u);
  uint32_t const expected[] = {0xa0000000u, 0x5fffffffu, 12582912u, (uint32_t)-12582912,
                               1u, UINT32_MAX, 2u, (uint32_t)-2,
                               0x0da740e0u, (uint32_t)-0x0da740e0};
  assert(memcmp(words, expected, sizeof(expected)) == 0);
  // Invalid packets must be rejected without partially replacing samples.
  float saved[10];
  memcpy(saved, samples, sizeof(saved));
  assert(audio_format_decode(samples, 5u, bytes, sizeof(bytes) - 1u, AUDIO_FORMAT_PCM32) == 0u);
  assert(audio_format_decode(samples, 4u, bytes, sizeof(bytes), AUDIO_FORMAT_PCM32) == 0u);
  assert(memcmp(saved, samples, sizeof(saved)) == 0);
}

static void test_pcm32_quantization(void) {
  float const samples[] = {-40000.0, 40000.0, -32768.0, 32768.0,
                             1.5 / 49152.0, -1.5 / 49152.0,
                             0.49 / 49152.0, -0.49 / 49152.0,
                             INFINITY, -INFINITY, NAN, 0.0};
  uint32_t words[12];
  assert(audio_format_pack_i2s(words, samples, 6u, AUDIO_FORMAT_PCM32) == 12u);
  uint32_t const expected[] = {0xa0000000u, 0x5fffffffu, 0xa0000000u, 0x5fffffffu,
                               2u, (uint32_t)-2, 0u, 0u,
                               0x5fffffffu, 0xa0000000u, 0u, 0u};
  assert(memcmp(words, expected, sizeof(expected)) == 0);
}

int main(void) {
  test_rate_matrix();
  test_idle_clock_negotiation();
  test_decode();
  test_pack();
  test_in_place_packing();
  test_pcm32_decode_and_pack();
  test_pcm32_quantization();
  return 0;
}
