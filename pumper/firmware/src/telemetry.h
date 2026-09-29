#ifndef TELEMETRY_H_
#define TELEMETRY_H_

#include <stdbool.h>
#include <stdint.h>

#define TELEMETRY_SCHEMA_VERSION 1u
#define TELEMETRY_FLAG_STREAMING 0x01u
#define TELEMETRY_FLAG_FEEDBACK_ACTIVE 0x02u
#define TELEMETRY_PAYLOAD_SIZE 56u

typedef struct {
  uint8_t schema_version;
  uint8_t flags;
  uint32_t uptime_seconds;
  uint32_t stream_duration_ms;
  uint32_t i2s_buffered_frames;
  uint32_t i2s_high_water_frames;
  int32_t feedback_correction_ppm;
  uint16_t average_dsp_load_basis_points;
  uint16_t peak_dsp_load_basis_points;
  uint32_t limiter_active_ms;
  uint32_t usb_audio_packets;
  uint32_t stream_starts;
  uint32_t malformed_hid_reports;
  uint32_t busy_hid_reports;
  uint32_t flash_failures;
  uint32_t meter_reports;
} telemetry_snapshot_t;

void telemetry_init(uint64_t now_us);
void telemetry_stream_started(uint64_t now_us, uint32_t stream_generation);
void telemetry_stream_stopped(uint64_t now_us);
void telemetry_record_audio_block(uint32_t stream_generation, uint32_t frames,
                                  uint32_t elapsed_us, uint32_t limiter_active_frames);
void telemetry_record_usb_audio_packet(void);
void telemetry_record_feedback(uint32_t actual_q16, uint32_t nominal_q16);
void telemetry_record_malformed_hid_report(void);
void telemetry_record_busy_hid_report(void);
void telemetry_record_flash_failure(void);
void telemetry_record_meter_report(void);
telemetry_snapshot_t telemetry_snapshot(uint64_t now_us, uint32_t sample_rate_hz,
                                        bool streaming, bool feedback_active,
                                        uint32_t i2s_buffered_frames,
                                        uint32_t i2s_high_water_frames);
void telemetry_encode(uint8_t payload[TELEMETRY_PAYLOAD_SIZE],
                      telemetry_snapshot_t const *snapshot);

#endif
