#include "pcm32_pack.h"
#include <string.h>
int32_t bench_pcm32_quantize(float sample) {
  uint32_t bits;
  memcpy(&bits, &sample, 4);
  uint32_t magnitude = bits & 0x7fffffff, negative = bits >> 31;
  if (magnitude > 0x7f800000)
    return 0; // NaN, matching the production quantizer.
  if (magnitude >= 0x47000000)
    return negative ? -1610612736 : 1610612735;
  uint32_t exponent = magnitude >> 23;
  // (1.fraction * 2^(exponent-127)) * (3 * 2^14).
  uint32_t product = ((magnitude & 0x7fffff) | 0x800000) * 3u;
  if (exponent >= 136)
    product <<= exponent - 136;
  else {
    unsigned shift = 136 - exponent;
    if (shift >= 27)
      return 0;
    product = (product + (1u << (shift - 1))) >> shift;
  }
  if (product > 1610612735u && !negative)
    product = 1610612735u;
  return negative ? -(int32_t)product : (int32_t)product;
}
size_t bench_pcm32_pack(uint32_t *out, float const *in, size_t frames) {
  for (size_t i = 0; i < frames; ++i) {
    uint32_t words[2] = {(uint32_t)bench_pcm32_quantize(in[2 * i]),
                         (uint32_t)bench_pcm32_quantize(in[2 * i + 1])};
    // Same channel compensation and overlapping-buffer contract as production.
    memcpy((unsigned char *)out + 8 * i, words, sizeof(words));
  }
  return 2 * frames;
}
