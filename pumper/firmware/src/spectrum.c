#include "spectrum.h"
#include "eq_protocol.h"
#include <math.h>
#include <string.h>
#include "spectrum_tables.h"

#define N SPECTRUM_FFT_SIZE
#define FIFO_MASK (SPECTRUM_FIFO_FRAMES - 1u)
typedef struct { float r, i; } pair_t;
// Keep the 127-tap filter in a mirrored 128-frame ring. The duplicate half
// makes both symmetric tap walks contiguous, with no division/modulo per tap.
#define DECIMATOR_RING 128u
typedef struct { pair_t history[2u * DECIMATOR_RING]; uint32_t index, phase; } decimator_t;
static pair_t s_fifo[SPECTRUM_FIFO_FRAMES];
static pair_t s_history[N], s_fft[N];
static float s_power[N / 2u + 1u];
static decimator_t s_decimator[2];
static uint32_t s_write, s_read, s_epoch, s_enabled;
// Sequence checked producer metadata; published at block END, after FIFO samples.
static uint32_t s_capture_meta_sequence, s_capture_epoch, s_capture_generation, s_capture_rate;
static uint32_t s_gap, s_capture_dropped, s_high_water, s_capture_max_us;
static uint32_t s_block_write, s_block_epoch, s_block_generation, s_block_rate, s_block_high_water;
static bool s_block_active;
static uint64_t s_deadline;
static uint32_t s_seen_epoch, s_seen_generation, s_seen_rate, s_seen_gap;
static uint32_t s_analysis_rate, s_factor, s_history_index, s_samples, s_hop;
static uint32_t s_stage, s_position, s_length, s_base, s_butterfly;
static uint32_t s_frame_sequence;
static bool s_result_ready;
static spectrum_frame_t s_result, s_build;
static spectrum_diagnostics_t s_diag;

_Static_assert(sizeof(s_fifo) + sizeof(s_history) + sizeof(s_fft) + sizeof(s_power) +
               sizeof(s_decimator) + sizeof(spectrum_window) + sizeof(spectrum_cos) +
               sizeof(spectrum_sin) + sizeof(spectrum_fir) + sizeof(spectrum_frequency) +
               sizeof(s_result) + sizeof(s_build) + sizeof(s_diag) + 1024u < 256u * 1024u,
               "Spectrum static storage exceeds budget");

static uint32_t load(uint32_t const *p) { return __atomic_load_n(p, __ATOMIC_ACQUIRE); }
static void store(uint32_t *p, uint32_t v) { __atomic_store_n(p, v, __ATOMIC_RELEASE); }
static void reset_analysis(void) {
  // Do not memset large buffers in the USB task. Counts invalidate old history.
  s_stage = s_position = s_history_index = s_samples = s_hop = 0u;
  memset(s_decimator, 0, sizeof(s_decimator));
  s_result_ready = false;
  store(&s_read, load(&s_write));
}
void spectrum_reset(void) {
  store(&s_epoch, load(&s_epoch) + 1u);
  reset_analysis();
}
void spectrum_start(uint64_t now_us) {
  spectrum_reset();
  s_frame_sequence = 0u;
  s_deadline = now_us + SPECTRUM_TIMEOUT_US;
  store(&s_enabled, 1u);
}
void spectrum_stop(void) {
  store(&s_enabled, 0u);
  s_deadline = 0u;
  spectrum_reset();
}
bool spectrum_keepalive(uint64_t now_us) {
  if (!spectrum_active(now_us)) return false;
  s_deadline = now_us + SPECTRUM_TIMEOUT_US;
  return true;
}
bool spectrum_active(uint64_t now_us) {
  if (!load(&s_enabled)) return false;
  if (now_us >= s_deadline) { spectrum_stop(); return false; }
  return true;
}
bool spectrum_capture_begin(uint32_t sample_rate_hz, uint32_t stream_generation) {
  s_block_active = load(&s_enabled) != 0u;
  if (!s_block_active) return false;
  s_block_write = load(&s_write);
  s_block_epoch = load(&s_epoch);
  s_block_generation = stream_generation;
  s_block_rate = sample_rate_hz;
  s_block_high_water = 0u;
  return true;
}
void spectrum_capture_pair(float left, float right) {
  uint32_t depth = s_block_write - load(&s_read);
  if (depth >= SPECTRUM_FIFO_FRAMES) {
    store(&s_capture_dropped, load(&s_capture_dropped) + 1u);
    store(&s_gap, load(&s_gap) + 1u);
    return;
  }
  s_fifo[s_block_write & FIFO_MASK] = (pair_t){left, right};
  ++s_block_write;
  if (depth + 1u > s_block_high_water) s_block_high_water = depth + 1u;
}
void spectrum_capture_end(uint32_t elapsed_us) {
  if (!s_block_active) return;
  if (s_block_high_water > load(&s_high_water)) store(&s_high_water, s_block_high_water);
  if (elapsed_us > load(&s_capture_max_us)) store(&s_capture_max_us, elapsed_us);
  __atomic_add_fetch(&s_capture_meta_sequence, 1u, __ATOMIC_ACQ_REL);
  store(&s_write, s_block_write);
  store(&s_capture_epoch, s_block_epoch);
  store(&s_capture_generation, s_block_generation);
  store(&s_capture_rate, s_block_rate);
  __atomic_add_fetch(&s_capture_meta_sequence, 1u, __ATOMIC_RELEASE);
  s_block_active = false;
}

static bool synchronize(void) {
  uint32_t seq = load(&s_capture_meta_sequence);
  if (seq & 1u) return false;
  uint32_t epoch = load(&s_capture_epoch), generation = load(&s_capture_generation);
  uint32_t rate = load(&s_capture_rate), gap = load(&s_gap);
  if (seq != load(&s_capture_meta_sequence) || epoch != load(&s_epoch)) return false;
  if (epoch != s_seen_epoch || generation != s_seen_generation || rate != s_seen_rate || gap != s_seen_gap) {
    s_seen_epoch = epoch; s_seen_generation = generation; s_seen_rate = rate; s_seen_gap = gap;
    s_factor = rate > 96000u ? 4u : rate > 48000u ? 2u : 1u;
    s_analysis_rate = rate / s_factor;
    reset_analysis();
  }
  return s_analysis_rate == 44100u || s_analysis_rate == 48000u;
}

static bool decimate(decimator_t *d, pair_t input, pair_t *output) {
  d->history[d->index] = d->history[d->index + DECIMATOR_RING] = input;
  d->index = (d->index + 1u) & (DECIMATOR_RING - 1u);
  d->phase ^= 1u;
  if (d->phase) return false;
  pair_t sum = {0};
  // Exploit half-band zeros and symmetry: 32 paired taps plus the center.
  pair_t const *newest = &d->history[d->index + DECIMATOR_RING - 1u];
  pair_t const *oldest = &d->history[d->index + 1u];
  for (uint32_t tap = 0u; tap < 63u; tap += 2u) {
    float coefficient = spectrum_fir[tap];
    sum.r += coefficient * (newest[-(int32_t)tap].r + oldest[tap].r);
    sum.i += coefficient * (newest[-(int32_t)tap].i + oldest[tap].i);
  }
  pair_t center = d->history[d->index + 64u];
  sum.r += spectrum_fir[63] * center.r;
  sum.i += spectrum_fir[63] * center.i;
  *output = sum;
  return true;
}

static uint32_t reverse_bits(uint32_t x) {
  uint32_t result = 0u;
  for (uint32_t bit = 0; bit < 12u; ++bit) { result = (result << 1u) | (x & 1u); x >>= 1u; }
  return result;
}
static void gather_one(void) {
  uint32_t read = load(&s_read);
  if (read == load(&s_write)) return;
  pair_t value = s_fifo[read & FIFO_MASK];
  store(&s_read, read + 1u);
  if (s_factor >= 2u && !decimate(&s_decimator[0], value, &value)) return;
  if (s_factor == 4u && !decimate(&s_decimator[1], value, &value)) return;
  s_history[s_history_index] = value;
  s_history_index = (s_history_index + 1u) & (N - 1u);
  if (s_samples < N) {
    ++s_samples;
    if (s_samples < N) return;
  } else if (++s_hop < s_analysis_rate / 20u) return;
  s_hop = 0u;
  if (s_stage != 0u) { ++s_diag.dropped_frames; return; }
  s_stage = 1u; s_position = 0u;
}
static void analyze_one(void) {
  if (s_stage == 1u) {
    uint32_t p = s_position++;
    pair_t v = s_history[(s_history_index + p) & (N - 1u)];
    float w = spectrum_window[p] / 32768.0f;
    s_fft[reverse_bits(p)] = (pair_t){v.r * w, v.i * w};
    if (s_position == N) { s_stage = 2u; s_length = 2u; s_base = s_butterfly = 0u; }
  } else if (s_stage == 2u) {
    uint32_t a = s_base + s_butterfly, b = a + s_length / 2u;
    uint32_t t = s_butterfly * (N / s_length);
    pair_t x = s_fft[a], y = s_fft[b];
    pair_t z = {y.r * spectrum_cos[t] - y.i * spectrum_sin[t],
                y.r * spectrum_sin[t] + y.i * spectrum_cos[t]};
    s_fft[a] = (pair_t){x.r + z.r, x.i + z.i};
    s_fft[b] = (pair_t){x.r - z.r, x.i - z.i};
    if (++s_butterfly == s_length / 2u) {
      s_butterfly = 0u; s_base += s_length;
      if (s_base == N) {
        s_base = 0u; s_length *= 2u;
        if (s_length > N) { s_stage = 3u; s_position = 0u; }
      }
    }
  } else if (s_stage == 3u) {
    uint32_t k = s_position++;
    pair_t a = s_fft[k], b = s_fft[(N - k) & (N - 1u)];
    float lr = (a.r + b.r) * 0.5f, li = (a.i - b.i) * 0.5f;
    float rr = (a.i + b.i) * 0.5f, ri = (b.r - a.r) * 0.5f;
    // Periodic Hann sum=N/2. Single-sided amplitude=2*|FFT|/(N/2).
    float scale = (k == 0u || k == N / 2u) ? 2.0f / N : 4.0f / N;
    s_power[k] = 0.5f * (lr*lr + li*li + rr*rr + ri*ri) * scale * scale;
    if (s_position == N / 2u + 1u) { s_stage = 4u; s_position = 0u; }
  } else if (s_stage == 4u) {
    uint32_t p = s_position++;
    float bin = spectrum_frequency[p] * N / s_analysis_rate;
    float low = (p ? sqrtf(spectrum_frequency[p-1u]*spectrum_frequency[p]) : 20.0f) * N / s_analysis_rate;
    float high = (p < 255u ? sqrtf(spectrum_frequency[p]*spectrum_frequency[p+1u]) : 20000.0f) * N / s_analysis_rate;
    float power;
    if (high - low < 1.0f) {
      uint32_t k = (uint32_t)bin;
      power = s_power[k] + (s_power[k+1u] - s_power[k]) * (bin-k);
    } else {
      power = 0.0f;
      for (uint32_t k = (uint32_t)ceilf(low); k <= (uint32_t)floorf(high); ++k)
        if (s_power[k] > power) power = s_power[k];
    }
    float code = power > 0.0f ? (10.0f * log10f(power) + 96.0f) * 2.0f : 0.0f;
    s_build.levels[p] = code <= 0.0f ? 0u : code >= 192.0f ? 192u : (uint8_t)(code + 0.5f);
    if (s_position == SPECTRUM_POINTS) {
      s_build.sequence = ++s_frame_sequence; s_build.sample_rate_hz = s_analysis_rate;
      s_result = s_build;
      s_result_ready = true; ++s_diag.completed_frames; s_stage = 0u;
    }
  }
}
void spectrum_task(uint64_t now_us, uint32_t (*clock_us)(void)) {
  if (!spectrum_active(now_us)) return;
  uint32_t started = clock_us();
  if (!synchronize()) return;
  do {
    // Freeze rolling history only while making the windowed FFT snapshot.
    if (s_stage != 1u) gather_one();
    // FFT work must not be tied one-to-one to expensive input filtering.
    // Advance a small batch before checking the slice clock; mapping stays
    // single-point because log conversion and peak intervals cost more.
    uint32_t batch = s_stage == 2u ? 8u : s_stage == 1u || s_stage == 3u ? 4u : 1u;
    for (uint32_t i = 0u; i < batch && s_stage != 0u; ++i) analyze_one();
    if (s_stage == 0u && load(&s_read) == load(&s_write)) break;
  } while ((uint32_t)(clock_us() - started) < 40u);
  // Recheck producer generation/gaps before any result can leave this slice.
  (void)synchronize();
  uint32_t elapsed = clock_us() - started;
  if (elapsed > s_diag.max_slice_us) s_diag.max_slice_us = elapsed;
}
bool spectrum_take_frame(spectrum_frame_t *frame) {
  if (!s_result_ready || !synchronize() || !s_result_ready) return false;
  *frame = s_result; s_result_ready = false; return true;
}
uint8_t spectrum_encode_chunk(uint8_t payload[56], spectrum_frame_t const *frame, uint8_t chunk) {
  if (chunk >= 6u) return 0u;
  uint8_t count = chunk == 5u ? 36u : 44u;
  memset(payload, 0, 56u);
  payload[0] = 1u; payload[1] = chunk; payload[2] = 6u; payload[3] = count;
  eq_protocol_write_u32(payload+4u, frame->sequence);
  eq_protocol_write_u32(payload+8u, frame->sample_rate_hz);
  memcpy(payload+12u, frame->levels+chunk*44u, count);
  return 12u + count;
}
spectrum_diagnostics_t spectrum_diagnostics(void) {
  spectrum_diagnostics_t result = s_diag;
  result.capture_dropped_frames = load(&s_capture_dropped);
  result.fifo_high_water = load(&s_high_water);
  result.max_capture_block_us = load(&s_capture_max_us);
  return result;
}
