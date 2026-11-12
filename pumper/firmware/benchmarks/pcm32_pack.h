#ifndef BENCH_PCM32_PACK_H
#define BENCH_PCM32_PACK_H
#include <float.h>
#include <stddef.h>
#include <stdint.h>
_Static_assert(sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128,
               "PCM32 packing requires IEEE binary32 input");
// Exact float -> PCM32 conversion at the existing 0.75 gain, without double arithmetic.
int32_t bench_pcm32_quantize(float sample);
size_t bench_pcm32_pack(uint32_t *out, float const *in, size_t frames);
#endif
