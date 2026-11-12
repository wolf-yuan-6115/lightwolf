#ifndef DEVICE_BENCHMARK_H
#define DEVICE_BENCHMARK_H
#include "audio_format.h"
#include "eq_dsp.h"
#include "i2s_out.h"
void bench_device_selftest(void);
uint32_t bench_device_clock_khz(audio_sample_format_t format, uint32_t rate);
void bench_device_task(bool streaming, uint32_t rate, audio_sample_format_t format,
                       i2s_out_diagnostics_t diagnostics, int32_t (*temperature)(void));
void bench_device_process(float *samples, size_t frames, eq_block_metrics_t *metrics, bool rms,
                          bool capture, uint32_t rate);
size_t bench_device_pack(uint32_t *out, float const *in, size_t frames,
                         audio_sample_format_t format);
void bench_device_finish(uint32_t processing_us, uint32_t packing_us, size_t frames);
#endif
