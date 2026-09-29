#include "telemetry.h"

#include <limits.h>
#include <string.h>

#include "eq_protocol.h"

_Static_assert(TELEMETRY_PAYLOAD_SIZE == EQ_PROTOCOL_TELEMETRY_PAYLOAD_SIZE,
               "telemetry payload sizes must match");

typedef struct {
  volatile uint32_t sequence;
  uint32_t stream_generation;
  uint64_t processed_frames;
  uint64_t processing_us;
  uint64_t limiter_active_frames;
  uint32_t peak_block_us;
  uint32_t peak_block_frames;
} audio_telemetry_t;

static audio_telemetry_t s_audio;
static uint64_t s_boot_us;
static uint64_t s_stream_started_us;
static bool s_stream_active;
static uint32_t s_stream_duration_ms;
static uint32_t s_expected_stream_generation;
static uint32_t s_feedback_q16;
static uint32_t s_feedback_nominal_q16;
static uint32_t s_usb_audio_packets;
static uint32_t s_stream_starts;
static uint32_t s_malformed_hid_reports;
static uint32_t s_busy_hid_reports;
static uint32_t s_flash_failures;
static uint32_t s_meter_reports;

static uint32_t saturating_increment(uint32_t value) {
  return value == UINT32_MAX ? value : value + 1u;
}

static uint32_t saturating_u32(uint64_t value) {
  return value > UINT32_MAX ? UINT32_MAX : (uint32_t)value;
}

static uint16_t dsp_load_basis_points(uint64_t processing_us, uint64_t frames,
                                      uint32_t sample_rate_hz) {
  if (!frames || !sample_rate_hz) return 0u;
  uint64_t load = processing_us * (sample_rate_hz / 100u) / frames;
  return load > UINT16_MAX ? UINT16_MAX : (uint16_t)load;
}

void telemetry_init(uint64_t now_us) {
  memset(&s_audio, 0, sizeof(s_audio));
  s_boot_us = now_us;
  s_stream_started_us = 0u;
  s_stream_active = false;
  s_stream_duration_ms = 0u;
  s_expected_stream_generation = 0u;
  s_feedback_q16 = s_feedback_nominal_q16 = 0u;
  s_usb_audio_packets = s_stream_starts = 0u;
  s_malformed_hid_reports = s_busy_hid_reports = 0u;
  s_flash_failures = s_meter_reports = 0u;
}

void telemetry_stream_started(uint64_t now_us, uint32_t stream_generation) {
  s_stream_started_us = now_us;
  s_stream_active = true;
  s_stream_duration_ms = 0u;
  s_expected_stream_generation = stream_generation;
  s_feedback_q16 = s_feedback_nominal_q16 = 0u;
  s_stream_starts = saturating_increment(s_stream_starts);
}

void telemetry_stream_stopped(uint64_t now_us) {
  if (s_stream_active)
    s_stream_duration_ms = saturating_u32((now_us - s_stream_started_us) / 1000u);
  s_stream_started_us = 0u;
  s_stream_active = false;
  s_feedback_q16 = s_feedback_nominal_q16 = 0u;
}

void telemetry_record_audio_block(uint32_t stream_generation, uint32_t frames,
                                  uint32_t elapsed_us, uint32_t limiter_active_frames) {
  __atomic_add_fetch(&s_audio.sequence, 1u, __ATOMIC_ACQUIRE);
  if (s_audio.stream_generation != stream_generation) {
    s_audio.stream_generation = stream_generation;
    s_audio.processed_frames = 0u;
    s_audio.processing_us = 0u;
    s_audio.limiter_active_frames = 0u;
    s_audio.peak_block_us = 0u;
    s_audio.peak_block_frames = 0u;
  }
  s_audio.processed_frames += frames;
  s_audio.processing_us += elapsed_us;
  s_audio.limiter_active_frames += limiter_active_frames;
  if (!s_audio.peak_block_frames ||
      (uint64_t)elapsed_us * s_audio.peak_block_frames >
          (uint64_t)s_audio.peak_block_us * frames) {
    s_audio.peak_block_us = elapsed_us;
    s_audio.peak_block_frames = frames;
  }
  __atomic_add_fetch(&s_audio.sequence, 1u, __ATOMIC_RELEASE);
}

void telemetry_record_usb_audio_packet(void) {
  s_usb_audio_packets = saturating_increment(s_usb_audio_packets);
}
void telemetry_record_feedback(uint32_t actual_q16, uint32_t nominal_q16) {
  s_feedback_q16 = actual_q16;
  s_feedback_nominal_q16 = nominal_q16;
}
void telemetry_record_malformed_hid_report(void) {
  s_malformed_hid_reports = saturating_increment(s_malformed_hid_reports);
}
void telemetry_record_busy_hid_report(void) {
  s_busy_hid_reports = saturating_increment(s_busy_hid_reports);
}
void telemetry_record_flash_failure(void) {
  s_flash_failures = saturating_increment(s_flash_failures);
}
void telemetry_record_meter_report(void) {
  s_meter_reports = saturating_increment(s_meter_reports);
}

telemetry_snapshot_t telemetry_snapshot(uint64_t now_us, uint32_t sample_rate_hz,
                                        bool streaming, bool feedback_active,
                                        uint32_t i2s_buffered_frames,
                                        uint32_t i2s_high_water_frames) {
  audio_telemetry_t audio;
  for (;;) {
    uint32_t before = __atomic_load_n(&s_audio.sequence, __ATOMIC_ACQUIRE);
    if (before & 1u) continue;
    audio = s_audio;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if (before == __atomic_load_n(&s_audio.sequence, __ATOMIC_RELAXED)) break;
  }
  if (audio.stream_generation != s_expected_stream_generation)
    audio = (audio_telemetry_t){0};

  telemetry_snapshot_t result = {
      .schema_version = TELEMETRY_SCHEMA_VERSION,
      .flags = (streaming ? TELEMETRY_FLAG_STREAMING : 0u) |
               (feedback_active ? TELEMETRY_FLAG_FEEDBACK_ACTIVE : 0u),
      .uptime_seconds = saturating_u32((now_us - s_boot_us) / 1000000u),
      .stream_duration_ms = streaming && s_stream_active
                                ? saturating_u32((now_us - s_stream_started_us) / 1000u)
                                : s_stream_duration_ms,
      .i2s_buffered_frames = i2s_buffered_frames,
      .i2s_high_water_frames = i2s_high_water_frames,
      .average_dsp_load_basis_points =
          dsp_load_basis_points(audio.processing_us, audio.processed_frames, sample_rate_hz),
      .peak_dsp_load_basis_points =
          dsp_load_basis_points(audio.peak_block_us, audio.peak_block_frames, sample_rate_hz),
      .limiter_active_ms = sample_rate_hz
                               ? saturating_u32((audio.limiter_active_frames / sample_rate_hz) * 1000u +
                                                (audio.limiter_active_frames % sample_rate_hz) * 1000u /
                                                    sample_rate_hz)
                               : 0u,
      .usb_audio_packets = s_usb_audio_packets,
      .stream_starts = s_stream_starts,
      .malformed_hid_reports = s_malformed_hid_reports,
      .busy_hid_reports = s_busy_hid_reports,
      .flash_failures = s_flash_failures,
      .meter_reports = s_meter_reports,
  };
  if (feedback_active && s_feedback_nominal_q16) {
    int64_t delta = (int64_t)s_feedback_q16 - s_feedback_nominal_q16;
    result.feedback_correction_ppm = (int32_t)(delta * 1000000 / s_feedback_nominal_q16);
  }
  return result;
}

void telemetry_encode(uint8_t payload[TELEMETRY_PAYLOAD_SIZE],
                      telemetry_snapshot_t const *snapshot) {
  memset(payload, 0, TELEMETRY_PAYLOAD_SIZE);
  payload[0] = snapshot->schema_version;
  payload[1] = snapshot->flags;
  eq_protocol_write_u32(payload + 4u, snapshot->uptime_seconds);
  eq_protocol_write_u32(payload + 8u, snapshot->stream_duration_ms);
  eq_protocol_write_u32(payload + 12u, snapshot->i2s_buffered_frames);
  eq_protocol_write_u32(payload + 16u, snapshot->i2s_high_water_frames);
  eq_protocol_write_i32(payload + 20u, snapshot->feedback_correction_ppm);
  eq_protocol_write_u16(payload + 24u, snapshot->average_dsp_load_basis_points);
  eq_protocol_write_u16(payload + 26u, snapshot->peak_dsp_load_basis_points);
  eq_protocol_write_u32(payload + 28u, snapshot->limiter_active_ms);
  eq_protocol_write_u32(payload + 32u, snapshot->usb_audio_packets);
  eq_protocol_write_u32(payload + 36u, snapshot->stream_starts);
  eq_protocol_write_u32(payload + 40u, snapshot->malformed_hid_reports);
  eq_protocol_write_u32(payload + 44u, snapshot->busy_hid_reports);
  eq_protocol_write_u32(payload + 48u, snapshot->flash_failures);
  eq_protocol_write_u32(payload + 52u, snapshot->meter_reports);
}
