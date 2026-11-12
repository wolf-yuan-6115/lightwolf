#ifndef BENCH_M33_MAC_H
#define BENCH_M33_MAC_H
#include <stdint.h>
#include <string.h>
// acc + floor(state * coefficient / 2^32), matching CMSIS mult32x64.
// Range/headroom is a caller precondition, as for the CMSIS block kernel.
static inline int64_t bench_m33_mac(int64_t acc, int64_t state, int32_t coefficient) {
#if defined(__arm__) && defined(__ARM_FEATURE_DSP)
  uint32_t alo = (uint32_t)acc, ahi = (uint32_t)((uint64_t)acc >> 32);
  uint32_t xlo = (uint32_t)state;
  int32_t xhi = (int32_t)(state >> 32);
  uint32_t scratch;
  int32_t fraction;
  __asm__("smull %[scratch], %[fraction], %[xlo], %[coef]\n"
          "and %[scratch], %[coef], %[xlo], asr #31\n"
          "add %[fraction], %[fraction], %[scratch]\n"
          "smlal %[alo], %[ahi], %[xhi], %[coef]\n"
          "adds %[alo], %[alo], %[fraction]\n"
          "adc %[ahi], %[ahi], %[fraction], asr #31\n"
          : [alo] "+&r"(alo), [ahi] "+&r"(ahi), [scratch] "=&r"(scratch), [fraction] "=&r"(fraction)
          : [xlo] "r"(xlo), [xhi] "r"(xhi), [coef] "r"(coefficient)
          : "cc");
  uint64_t bits = (uint64_t)ahi << 32 | alo;
  memcpy(&acc, &bits, sizeof(acc));
  return acc;
#else
  return acc + (((int64_t)(uint32_t)state * coefficient) >> 32) + ((state >> 32) * coefficient);
#endif
}
#endif
