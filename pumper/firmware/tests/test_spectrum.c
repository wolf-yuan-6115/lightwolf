#include "spectrum.h"
#include "eq_protocol.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static uint32_t s_clock;
static uint32_t fake_clock(void) { return ++s_clock; }
static void drain(uint64_t now) {
  for (unsigned i = 0; i < 100u; ++i) spectrum_task(now, fake_clock);
}
static spectrum_frame_t feed_tone(uint32_t rate, float hz, float gain, bool opposite, uint32_t generation, bool start) {
  if (start) spectrum_start(0u);
  // First block acknowledges the new subscription; the consumer flushes it.
  assert(spectrum_capture_begin(rate, generation));
  spectrum_capture_pair(0, 0); spectrum_capture_end(1u); drain(0u);
  spectrum_frame_t result = {0};
  bool received = false;
  for (uint32_t offset = 0; offset < rate / 3u; offset += 128u) {
    uint64_t now = (uint64_t)offset * 1000000u / rate;
    assert(spectrum_capture_begin(rate, generation));
    for (uint32_t i = 0; i < 128u; ++i) {
      float value = gain * 32768.0f * (float)sin(6.283185307179586 * hz * (offset + i) / rate);
      spectrum_capture_pair(value, opposite ? -value : value);
    }
    spectrum_capture_end(2u);
    drain(now);
    spectrum_frame_t frame;
    if (spectrum_take_frame(&frame)) { result = frame; received = true; }
  }
  assert(received);
  assert(result.sample_rate_hz == (rate % 44100u == 0u ? 44100u : 48000u));
  return result;
}
static spectrum_frame_t tone(uint32_t rate, float hz, float gain, bool opposite, uint32_t generation) {
  spectrum_frame_t result = feed_tone(rate, hz, gain, opposite, generation, true);
  spectrum_stop();
  return result;
}
static uint8_t maximum(spectrum_frame_t const *f) {
  uint8_t peak = 0;
  for (unsigned i = 0; i < SPECTRUM_POINTS; ++i) if (f->levels[i] > peak) peak = f->levels[i];
  return peak;
}
// A fixed number of short slices between capture blocks must advance both
// acquisition and the FFT. This catches coupling one FFT operation to every
// expensive decimator call; the fake clock is a work budget, not hardware timing.
static void test_incremental_cadence(void) {
  uint32_t rates[] = {44100u, 48000u, 88200u, 96000u, 176400u, 192000u};
  for (unsigned r = 0u; r < 6u; ++r) {
    uint32_t rate = rates[r], frames = 0u;
    spectrum_start(0u);
    for (uint32_t offset = 0u; offset < rate / 2u; offset += 128u) {
      assert(spectrum_capture_begin(rate, r + 100u));
      for (uint32_t i = 0u; i < 128u; ++i) spectrum_capture_pair(100.0f, -100.0f);
      spectrum_capture_end(1u);
      uint64_t now = (uint64_t)offset * 1000000u / rate;
      for (unsigned slice = 0u; slice < 8u; ++slice) spectrum_task(now, fake_clock);
      spectrum_frame_t frame;
      if (spectrum_take_frame(&frame)) ++frames;
    }
    assert(frames >= 7u);
    spectrum_stop();
  }
}
// First complete frame must arrive within 60 ms of contiguous capture at every
// input rate. The fake clock verifies acquisition/work scheduling, not device time.
static void test_short_window_latency(void) {
  uint32_t rates[] = {44100u, 48000u, 88200u, 96000u, 176400u, 192000u};
  for (unsigned r = 0u; r < 6u; ++r) {
    uint32_t rate = rates[r];
    spectrum_start(0u);
    assert(spectrum_capture_begin(rate, r + 200u));
    spectrum_capture_pair(0, 0);
    spectrum_capture_end(1u);
    drain(0u);
    bool received = false;
    for (uint32_t offset = 0u; offset < rate * 60u / 1000u; offset += 128u) {
      assert(spectrum_capture_begin(rate, r + 200u));
      for (uint32_t i = 0u; i < 128u; ++i) spectrum_capture_pair(100, -100);
      spectrum_capture_end(1u);
      drain((uint64_t)(offset + 128u) * 1000000u / rate);
      spectrum_frame_t frame;
      if (spectrum_take_frame(&frame)) {
        // No partial window may be published to achieve the lower latency.
        uint32_t analysis_rate = rate % 44100u == 0u ? 44100u : 48000u;
        assert(offset + 128u >= SPECTRUM_FFT_SIZE * (rate / analysis_rate));
        received = true;
        break;
      }
    }
    assert(received);
    spectrum_stop();
  }
}
int main(void) {
  test_short_window_latency();
  test_incremental_cadence();
  spectrum_frame_t silence = tone(48000u, 1000, 0, false, 1u);
  assert(maximum(&silence) == 0);
  // A high-frequency bin-centered tone lies in a max-bin logarithmic interval.
  float hz = 48000.0f * 500.0f / (float)SPECTRUM_FFT_SIZE;
  spectrum_frame_t full = tone(48000u, hz, 1, false, 1u);
  spectrum_frame_t inverted = tone(48000u, hz, 1, true, 1u);
  assert(maximum(&full) >= 191u);
  assert(memcmp(full.levels, inverted.levels, 256u) == 0);
  spectrum_frame_t quiet = tone(48000u, hz, 0.5f, false, 1u);
  assert(maximum(&quiet) >= 179u && maximum(&quiet) <= 181u);
  unsigned peak_index = 0;
  for (unsigned i = 1; i < 256u; ++i) if (full.levels[i] > full.levels[peak_index]) peak_index = i;
  float expected = 255.0f * logf(hz / 20.0f) / logf(1000.0f);
  assert(fabsf(peak_index - expected) < 2.0f);
  for (unsigned r = 0; r < 6u; ++r) {
    uint32_t rates[] = {44100u, 48000u, 88200u, 96000u, 176400u, 192000u};
    uint32_t rate = rates[r], analysis = rate % 44100u == 0u ? 44100u : 48000u;
    spectrum_frame_t pass = tone(rate, analysis * floorf(20000.0f * (float)SPECTRUM_FFT_SIZE / analysis) / (float)SPECTRUM_FFT_SIZE, 1, false, r + 2u);
    assert(maximum(&pass) >= 189u); // Near 18-20 kHz passband remains within 1.5 dB.
    if (rate > 48000u) {
      spectrum_frame_t alias = tone(rate, analysis - 6000.0f, 1, false, r + 2u);
      assert(maximum(&alias) <= 40u); // At least 76 dB below full-scale alias.
    }
  }
  spectrum_frame_t before_reset = feed_tone(48000u, hz, 1, false, 10u, true);
  spectrum_reset();
  spectrum_frame_t after_reset = feed_tone(44100u, 44100.0f * 500.0f / (float)SPECTRUM_FFT_SIZE, 0.5f, true, 11u, false);
  assert(after_reset.sequence > before_reset.sequence);
  assert(maximum(&after_reset) >= 179u && maximum(&after_reset) <= 181u);
  spectrum_stop();
  uint8_t joined[256], payload[56];
  for (uint8_t i = 0; i < 6u; ++i) {
    uint8_t size = spectrum_encode_chunk(payload, &full, i);
    assert(size == (i == 5u ? 48u : 56u));
    assert(payload[0] == 1u && payload[1] == i && payload[2] == 6u);
    assert(eq_protocol_read_u32(payload + 4u) == full.sequence);
    assert(eq_protocol_read_u32(payload + 8u) == 48000u);
    memcpy(joined + i * 44u, payload + 12u, payload[3]);
  }
  assert(memcmp(joined, full.levels, 256u) == 0);
  assert(spectrum_encode_chunk(payload, &full, 6u) == 0u);
  spectrum_start(100u);
  assert(spectrum_active(100u + SPECTRUM_TIMEOUT_US - 1u));
  assert(spectrum_keepalive(1000u));
  assert(!spectrum_active(1000u + SPECTRUM_TIMEOUT_US));
  assert(!spectrum_keepalive(1000u + SPECTRUM_TIMEOUT_US));
  spectrum_start(0u);
  assert(spectrum_capture_begin(48000u, 99u));
  for (unsigned i = 0; i < SPECTRUM_FIFO_FRAMES + 10u; ++i) spectrum_capture_pair(100, 100);
  spectrum_capture_end(9u);
  spectrum_diagnostics_t d = spectrum_diagnostics();
  assert(d.capture_dropped_frames >= 10u && d.fifo_high_water == SPECTRUM_FIFO_FRAMES);
  drain(0u);
  assert(!spectrum_take_frame(&full));
  spectrum_reset();
  drain(0u);
  assert(!spectrum_take_frame(&full));
  spectrum_stop();
  assert(!spectrum_capture_begin(48000u, 99u));
  puts("spectrum tests passed");
}
