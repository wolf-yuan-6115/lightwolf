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
  }
  assert(audio_format_rate_valid(AUDIO_FORMAT_PCM16, 176400u));
  assert(audio_format_rate_valid(AUDIO_FORMAT_PCM16, 192000u));
  assert(!audio_format_rate_valid(AUDIO_FORMAT_PCM24, 176400u));
  assert(!audio_format_rate_valid(AUDIO_FORMAT_PCM24, 192000u));
  assert(!audio_format_rate_valid(AUDIO_FORMAT_PCM16, 48001u));
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
}

static void test_decode(void) {
  uint8_t pcm16[] = {0x00, 0x80, 0xff, 0x7f, 0x34, 0x12, 0xcc, 0xed};
  double samples[4];
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
  double samples[] = {-32768.0, 32767.0, -40000.0, 40000.0, 1.4, -1.6};
  uint32_t words[6] = {0};
  assert(audio_format_pack_i2s(words, samples, 3u, AUDIO_FORMAT_PCM16) == 3u);
  assert(words[0] == 0xa0005fffu);
  assert(words[1] == 0xa0005fffu);
  assert(words[2] == 0x0001ffffu);

  double pcm24[] = {-32768.0, 32768.0, -40000.0, 40000.0, 1.0, -1.0};
  assert(audio_format_pack_i2s(words, pcm24, 3u, AUDIO_FORMAT_PCM24) == 6u);
  assert(words[0] == 0xa0000000u);
  assert(words[1] == 0x5fffff00u);
  assert(words[2] == 0xa0000000u);
  assert(words[3] == 0x5fffff00u);
  assert(words[4] == 0x0000c000u);
  assert(words[5] == 0xffff4000u);
}

static void test_in_place_packing(void) {
  double const samples[] = {-32768.0, 32767.0, 12345.123, -23456.456, 1.5, -1.5};
  audio_sample_format_t formats[] = {AUDIO_FORMAT_PCM16, AUDIO_FORMAT_PCM24};
  for (unsigned f = 0; f < 2u; ++f) {
    union { double samples[6]; uint32_t words[6]; } block;
    uint32_t expected[6];
    memcpy(block.samples, samples, sizeof(samples));
    size_t count = audio_format_pack_i2s(expected, samples, 3u, formats[f]);
    assert(audio_format_pack_i2s(block.words, block.samples, 3u, formats[f]) == count);
    assert(memcmp(block.words, expected, count * sizeof(uint32_t)) == 0);
  }
}

int main(void) {
  test_rate_matrix();
  test_idle_clock_negotiation();
  test_decode();
  test_pack();
  test_in_place_packing();
  return 0;
}
