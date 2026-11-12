#include "audio_format.h"
#include "pcm32_pack.h"
#include <assert.h>
#include <math.h>
#include <string.h>
static void check(float x) {
  float pair[2] = {x, -x};
  uint32_t expected[2], actual[2];
  assert(audio_format_pack_i2s(expected, pair, 1, AUDIO_FORMAT_PCM32) == 2);
  assert(bench_pcm32_pack(actual, pair, 1) == 2);
  assert(memcmp(actual, expected, sizeof(actual)) == 0);
  union {
    float samples[2];
    uint32_t words[2];
  } block;
  memcpy(block.samples, pair, sizeof(pair));
  bench_pcm32_pack(block.words, block.samples, 1);
  assert(memcmp(block.words, expected, sizeof(expected)) == 0);
}
int main(void) {
  float cases[] = {0, -0.0f, 32768, -32768, INFINITY, -INFINITY, NAN, 1.0f / 65536, 1.5f / 49152};
  for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
    check(cases[i]);
  uint32_t seed = 0x93a46f21;
  for (unsigned i = 0; i < 2000000; ++i) {
    seed = 1664525 * seed + 1013904223;
    float x;
    memcpy(&x, &seed, 4);
    check(x);
  }
  // Values around rounding thresholds and both output ceilings.
  for (unsigned n = 0; n < 100000; ++n) {
    float x = (n + 0.5f) / 49152.0f;
    check(x);
    check(nextafterf(x, 0));
    check(nextafterf(x, INFINITY));
  }
  check(nextafterf(32768, 0));
  check(nextafterf(-32768, 0));
}
