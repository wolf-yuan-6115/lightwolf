#ifndef PUMPER_SPECTRUM_H
#define PUMPER_SPECTRUM_H
#include <stdbool.h>
#include <stdint.h>
#define SPECTRUM_POINTS 256u
#define SPECTRUM_FFT_SIZE 2048u
#define SPECTRUM_FIFO_FRAMES 16384u
#define SPECTRUM_TIMEOUT_US 1250000u

typedef struct {
  uint32_t sequence, sample_rate_hz;
  uint8_t levels[SPECTRUM_POINTS];
} spectrum_frame_t;
// Development diagnostics: only capture counters are written by core 1.
typedef struct {
  uint32_t completed_frames, dropped_frames, fifo_high_water, capture_dropped_frames;
  uint32_t max_slice_us, max_capture_block_us;
} spectrum_diagnostics_t;

// Core 0 owns subscription, analysis and result operations.
void spectrum_start(uint64_t now_us);
bool spectrum_keepalive(uint64_t now_us);
void spectrum_stop(void);
void spectrum_reset(void);
bool spectrum_active(uint64_t now_us);
void spectrum_task(uint64_t now_us, uint32_t (*clock_us)(void));
bool spectrum_take_frame(spectrum_frame_t *frame);
uint8_t spectrum_encode_chunk(uint8_t payload[56], spectrum_frame_t const *frame, uint8_t chunk);
spectrum_diagnostics_t spectrum_diagnostics(void);
// Core 1 owns capture. begin/end bracket one audio block; pair is called in the DSP loop.
bool spectrum_capture_begin(uint32_t sample_rate_hz, uint32_t stream_generation);
void spectrum_capture_pair(float left, float right);
void spectrum_capture_end(uint32_t elapsed_us);
#endif
