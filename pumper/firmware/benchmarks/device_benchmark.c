#include "device_benchmark.h"
#include "audio_controls.h"
#include "fixed.h"
#include "hardware/clocks.h"
#include "pico/stdlib.h"
#include "spectrum.h"
#include "tusb.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t request, applied = UINT32_MAX, local_rate;
static bench_fixed_t fixed;
static bench_ref_stage_t ref[10];
static bench_coeff_t coefficients[10];
static eq_config_t config;
static int32_t in[388], out[388];
static uint32_t frame_index, seed = 0x93a46f21, mode, scenario, headroom;
static uint32_t preparation_us, eq_us, rest_us, convert_us, generator_us;
typedef struct {
  uint32_t seq, blocks, frames, peak_us, peak_frames, max_us, prep_max, eq_max, rest_max,
      convert_max, generator_max, pack_max;
  uint64_t total_us;
  uint32_t overflow, clip, unstable;
} result_t;
static result_t result;
static void update_coefficients(bool transition) {
  if (mode == 0) {
    if (transition)
      eq_set_config(&config);
    else
      eq_init(local_rate, &config);
    return;
  }
  for (unsigned i = 0; i < 10; ++i)
    bench_coefficients(&coefficients[i], &config.filters[i], local_rate);
  if (mode == 1)
    bench_reference_reset(ref, coefficients, 10);
  else if (transition)
    bench_fixed_ramp(&fixed, coefficients, local_rate / 100);
  else
    bench_fixed_init(&fixed, coefficients, mode == 3);
}
bool bench_device_boost(void) {
  uint32_t r = __atomic_load_n(&request, __ATOMIC_ACQUIRE);
  return (r & 0x10000u) && (r & 3u) >= 2;
}
static void measure(eq_level_metrics_t *m, float l, float r, bool rms) {
  float v[2] = {l, r};
  uint32_t a[2];
  for (unsigned i = 0; i < 2; ++i) {
    float x = fabsf(v[i]);
    a[i] = x >= 32768 ? 32768 : (uint32_t)(x + 0.5f);
  }
  if (a[0] > m->left_peak)
    m->left_peak = a[0];
  if (a[1] > m->right_peak)
    m->right_peak = a[1];
  if (rms) {
    m->left_square_sum += (uint64_t)a[0] * a[0];
    m->right_square_sum += (uint64_t)a[1] * a[1];
  }
}
void bench_device_process(float *samples, size_t frames, eq_block_metrics_t *metrics, bool rms,
                          bool capture, uint32_t rate) {
  uint32_t start = time_us_32(), r = __atomic_load_n(&request, __ATOMIC_ACQUIRE);
  if (r != applied || rate != local_rate) {
    applied = r;
    local_rate = rate;
    mode = r & 3u;
    scenario = (r >> 4) & 3u;
    headroom = (r >> 8) & 31u;
    frame_index = 0;
    seed = 0x93a46f21;
    bench_fixture(&config, 0);
    update_coefficients(false);
    audio_controls_init(rate, &k_crossfeed_default, &k_output_processing_default);
    if (scenario) {
      crossfeed_config_t cf = {CROSSFEED_CUSTOM, 4000, 300, 600};
      audio_controls_set_crossfeed(&cf);
      float g[2] = {4, 4};
      audio_controls_set_gains(g);
    }
    __atomic_add_fetch(&result.seq, 1, __ATOMIC_ACQ_REL);
    memset((char *)&result + sizeof(result.seq), 0, sizeof(result) - sizeof(result.seq));
    __atomic_add_fetch(&result.seq, 1, __ATOMIC_RELEASE);
  }
  if (scenario == 2 && frame_index && frame_index % (rate / 50) < frames) {
    // Every 20 ms retarget all ten bands and global output controls.
    bench_fixture(&config, 0);
    bool alternate = (frame_index / (rate / 50)) & 1;
    for (unsigned i = 0; i < 10; ++i)
      config.filters[i].frequency_hz *= alternate ? 1.02f : 1.0f;
    update_coefficients(true);
    output_processing_config_t output = {alternate ? OUTPUT_PROCESSING_SWAP : 0,
                                         alternate ? 2000 : -2000, alternate ? 20000 : 10000};
    audio_controls_set_output_processing(&output);
    crossfeed_config_t cf = {CROSSFEED_CUSTOM, alternate ? 4000 : 1000, alternate ? 300 : 2000,
                             alternate ? 600 : 0};
    audio_controls_set_crossfeed(&cf);
  }
  preparation_us = time_us_32() - start;
  start = time_us_32();
  for (size_t n = 0; n < frames; ++n) {
    // Reproducible nonzero wideband excitation; never used as I2S output.
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    int32_t v = (int32_t)(seed & 0x7fffffffu) - 1073741824;
    if (scenario && (frame_index + n) % (rate / 10) < rate / 20)
      v = v < 0 ? INT32_MIN / 2 : INT32_MAX / 2;
    in[2 * n] = (int32_t)bench_round_shift(v, headroom);
    in[2 * n + 1] = -in[2 * n];
    samples[2 * n] = (float)v / 65536.0f;
    samples[2 * n + 1] = -samples[2 * n];
  }
  generator_us = time_us_32() - start;
  memset(metrics, 0, sizeof(*metrics));
  start = time_us_32();
  if (mode == 0) {
    eq_process_interleaved_stereo(samples, frames, metrics, rms);
    eq_us = time_us_32() - start;
    rest_us = convert_us = 0;
  } else {
    for (size_t n = 0; n < frames; ++n)
      for (unsigned ch = 0; ch < 2; ++ch) {
        if (mode == 1)
          samples[n * 2 + ch] =
              (float)(bench_reference_sample(ref, 10, ch, samples[n * 2 + ch] / 32768.0) * 32768.0);
        else
          out[n * 2 + ch] = bench_fixed_sample(&fixed, ch, in[n * 2 + ch]);
      }
    eq_us = time_us_32() - start;
    start = time_us_32();
    if (mode >= 2)
      for (size_t i = 0; i < frames * 2; ++i)
        samples[i] = ldexpf((float)out[i], (int)headroom - 16);
    convert_us = time_us_32() - start;
    start = time_us_32();
    for (size_t n = 0; n < frames; ++n) {
      float *l = &samples[n * 2], *rr = &samples[n * 2 + 1];
      // Benchmark metrics measure the generated input and post-controls output.
      measure(&metrics->pre_eq, ldexpf((float)in[n * 2], (int)headroom - 16),
              ldexpf((float)in[n * 2 + 1], (int)headroom - 16), rms);
      metrics->limiter_active_frames += audio_controls_process(l, rr);
      if (capture)
        spectrum_capture_pair(*l, *rr);
      measure(&metrics->post_eq, *l, *rr, rms);
    }
    rest_us = time_us_32() - start;
  }
  frame_index += (uint32_t)frames;
}
#define BENCH_MAX(field, value)                                                                    \
  do {                                                                                             \
    if ((value) > result.field)                                                                    \
      result.field = (value);                                                                      \
  } while (0)
void bench_device_finish(uint32_t process, uint32_t pack, size_t frames) {
  __atomic_add_fetch(&result.seq, 1, __ATOMIC_ACQ_REL);
  ++result.blocks;
  result.frames += (uint32_t)frames;
  result.total_us += process + pack;
  if (!result.peak_frames ||
      (uint64_t)(process + pack) * result.peak_frames > (uint64_t)result.peak_us * frames) {
    result.peak_us = process + pack;
    result.peak_frames = (uint32_t)frames;
  }
  BENCH_MAX(max_us, process + pack);
  BENCH_MAX(prep_max, preparation_us);
  BENCH_MAX(eq_max, eq_us);
  BENCH_MAX(rest_max, rest_us);
  BENCH_MAX(convert_max, convert_us);
  BENCH_MAX(generator_max, generator_us);
  BENCH_MAX(pack_max, pack);
  result.overflow = fixed.overflows;
  result.clip = fixed.clips;
  result.unstable = fixed.unstable;
  __atomic_add_fetch(&result.seq, 1, __ATOMIC_RELEASE);
}
static void reply(char const *s) {
  size_t n = strlen(s);
  if (tud_cdc_write_available() >= n) {
    tud_cdc_write(s, n);
    tud_cdc_write_flush();
  }
}
void bench_device_task(bool streaming, uint32_t rate, audio_sample_format_t format,
                       i2s_out_diagnostics_t diag, int32_t (*temperature)(void)) {
  static char line[96];
  static unsigned pos;
  while (tud_cdc_available()) {
    char c = (char)tud_cdc_read_char();
    if (c == '\r')
      continue;
    if (c != '\n') {
      if (pos < sizeof(line) - 1)
        line[pos++] = c;
      continue;
    }
    line[pos] = 0;
    pos = 0;
    if (!strcmp(line, "stats")) {
      result_t s;
      uint32_t before = __atomic_load_n(&result.seq, __ATOMIC_ACQUIRE);
      if (before & 1) {
        reply("{\"busy\":true}\n");
        continue;
      }
      s = result;
      __atomic_thread_fence(__ATOMIC_ACQUIRE);
      if (before != __atomic_load_n(&result.seq, __ATOMIC_RELAXED)) {
        reply("{\"busy\":true}\n");
        continue;
      }
      spectrum_diagnostics_t spec = spectrum_diagnostics();
      char outbuf[768];
      snprintf(
          outbuf, sizeof(outbuf),
          "{\"mode\":%u,\"scenario\":%u,\"rate\":%u,\"bits\":%u,\"clock\":%lu,\"frames\":%u,"
          "\"blocks\":%u,\"total_us\":%llu,\"peak_us\":%u,\"peak_frames\":%u,\"max_us\":%u,\"prep_"
          "us\":%u,\"eq_us\":%u,\"rest_us\":%u,\"convert_us\":%u,\"generator_us\":%u,\"pack_us\":%"
          "u,\"overflows\":%u,\"clips\":%u,\"unstable\":%u,\"underruns\":%u,\"low_water\":%u,"
          "\"spectrum_slice_us\":%u,\"spectrum_drops\":%u,\"temperature_mc\":%ld}\n",
          mode, scenario, rate, audio_format_bit_depth(format),
          (unsigned long)clock_get_hz(clk_sys), s.frames, s.blocks, (unsigned long long)s.total_us,
          s.peak_us, s.peak_frames, s.max_us, s.prep_max, s.eq_max, s.rest_max, s.convert_max,
          s.generator_max, s.pack_max, s.overflow, s.clip, s.unstable, diag.underrun_frames,
          diag.low_water_frames, spec.max_slice_us, spec.capture_dropped_frames,
          (long)temperature());
      reply(outbuf);
    } else {
      unsigned m, sc, h, b;
      if (sscanf(line, "run %u %u %u %u", &m, &sc, &h, &b) != 4 || m > 3 || sc > 2 || h > 30 ||
          b > 1 || streaming) {
        reply("{\"error\":\"run requires idle and valid mode/scenario/headroom/boost\"}\n");
        continue;
      }
      // A generation bit forces reset when repeating an identical run.
      uint32_t old = __atomic_load_n(&request, __ATOMIC_RELAXED);
      __atomic_store_n(&request, ((old ^ 0x1000000u) & 0x1000000u) | m | sc << 4 | h << 8 | b << 16,
                       __ATOMIC_RELEASE);
      reply("{\"ready\":true}\n");
    }
  }
}
