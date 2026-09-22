#include <assert.h>
#include <math.h>
#include <stdint.h>

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

static void test_decode(void) {
  uint8_t pcm16[] = {0x00, 0x80, 0xff, 0x7f, 0x34, 0x12, 0xcc, 0xed};
  float samples[4];
  assert(audio_format_decode(samples, 2u, pcm16, sizeof(pcm16), AUDIO_FORMAT_PCM16) == 2u);
  assert(samples[0] == -32768.0f && samples[1] == 32767.0f);
  assert(samples[2] == 4660.0f && samples[3] == -4660.0f);

  uint8_t pcm24[] = {0x00, 0x00, 0x80, 0xff, 0xff, 0x7f,
                     0x00, 0x01, 0x00, 0x00, 0xff, 0xff};
  assert(audio_format_decode(samples, 2u, pcm24, sizeof(pcm24), AUDIO_FORMAT_PCM24) == 2u);
  assert(samples[0] == -32768.0f);
  assert(fabsf(samples[1] - (8388607.0f / 256.0f)) < 0.01f);
  assert(samples[2] == 1.0f && samples[3] == -1.0f);
  assert(audio_format_decode(samples, 2u, pcm24, sizeof(pcm24) - 1u, AUDIO_FORMAT_PCM24) == 0u);
  assert(audio_format_decode(samples, 1u, pcm24, sizeof(pcm24), AUDIO_FORMAT_PCM24) == 0u);
}

static void test_pack(void) {
  float samples[] = {-32768.0f, 32767.0f, -40000.0f, 40000.0f, 1.4f, -1.6f};
  uint32_t words[6] = {0};
  assert(audio_format_pack_i2s(words, samples, 3u, AUDIO_FORMAT_PCM16) == 3u);
  assert(words[0] == 0xa0005fffu);
  assert(words[1] == 0xa0005fffu);
  assert(words[2] == 0x0001ffffu);

  float pcm24[] = {-32768.0f, 32768.0f, -40000.0f, 40000.0f, 1.0f, -1.0f};
  assert(audio_format_pack_i2s(words, pcm24, 3u, AUDIO_FORMAT_PCM24) == 6u);
  assert(words[0] == 0xa0000000u);
  assert(words[1] == 0x5fffff00u);
  assert(words[2] == 0xa0000000u);
  assert(words[3] == 0x5fffff00u);
  assert(words[4] == 0x0000c000u);
  assert(words[5] == 0xffff4000u);
}

int main(void) {
  test_rate_matrix();
  test_decode();
  test_pack();
  return 0;
}
