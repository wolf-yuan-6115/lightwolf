#include <assert.h>
#include <stdint.h>

#include "eq_protocol.h"
#include "telemetry.h"

int main(void) {
  uint32_t const nominal_q16 = (48000u << 16u) / 1000u;
  telemetry_init(1000000u);
  telemetry_stream_started(2000000u, 7u);
  telemetry_record_audio_block(7u, 480u, 1000u, 240u);
  telemetry_record_usb_audio_packet();
  telemetry_record_feedback(nominal_q16 + 3146u, nominal_q16);
  telemetry_record_malformed_hid_report();
  telemetry_record_busy_hid_report();
  telemetry_record_flash_failure();
  telemetry_record_meter_report();

  telemetry_snapshot_t snapshot = telemetry_snapshot(2500000u, 48000u, true, true, 144u, 288u);
  assert(snapshot.schema_version == 1u);
  assert(snapshot.flags == (TELEMETRY_FLAG_STREAMING | TELEMETRY_FLAG_FEEDBACK_ACTIVE));
  assert(snapshot.uptime_seconds == 1u);
  assert(snapshot.stream_duration_ms == 500u);
  assert(snapshot.i2s_buffered_frames == 144u);
  assert(snapshot.i2s_high_water_frames == 288u);
  assert(snapshot.feedback_correction_ppm == 1000);
  assert(snapshot.average_dsp_load_basis_points == 1000u);
  assert(snapshot.peak_dsp_load_basis_points == 1000u);
  assert(snapshot.limiter_active_ms == 5u);
  assert(snapshot.usb_audio_packets == 1u && snapshot.stream_starts == 1u);
  assert(snapshot.malformed_hid_reports == 1u && snapshot.busy_hid_reports == 1u);
  assert(snapshot.flash_failures == 1u && snapshot.meter_reports == 1u);

  uint8_t payload[TELEMETRY_PAYLOAD_SIZE];
  telemetry_encode(payload, &snapshot);
  assert(payload[0] == 1u && payload[1] == 3u && payload[2] == 0u && payload[3] == 0u);
  assert(eq_protocol_read_u32(payload + 4u) == 1u);
  assert(eq_protocol_read_i32(payload + 20u) == 1000);
  assert(eq_protocol_read_u16(payload + 24u) == 1000u);
  assert(eq_protocol_read_u32(payload + 52u) == 1u);

  telemetry_stream_stopped(3000000u);
  snapshot = telemetry_snapshot(4000000u, 48000u, false, false, 0u, 288u);
  assert(snapshot.stream_duration_ms == 1000u);
  assert(snapshot.feedback_correction_ppm == 0);
  assert(snapshot.limiter_active_ms == 5u);

  telemetry_stream_started(5000000u, 8u);
  snapshot = telemetry_snapshot(5000000u, 48000u, true, false, 0u, 0u);
  assert(snapshot.average_dsp_load_basis_points == 0u);
  assert(snapshot.limiter_active_ms == 0u);
  assert(snapshot.stream_starts == 2u);

  telemetry_record_audio_block(8u, 1u, UINT32_MAX, 0u);
  snapshot = telemetry_snapshot(5000001u, 192000u, true, false, 0u, 0u);
  assert(snapshot.average_dsp_load_basis_points == UINT16_MAX);
  assert(snapshot.peak_dsp_load_basis_points == UINT16_MAX);
  return 0;
}
