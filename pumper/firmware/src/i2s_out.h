// Public API for the I2S output driver (i2s_out.c).
//
// All functions are safe to call from the main task.
// Calls use a cross-core critical section to protect the ring shared with DMA.

#ifndef I2S_OUT_H_
#define I2S_OUT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Initialise the I2S PIO state machine and DMA channel, then start playback
// at the given sample rate (Hz).  Must be called once before any other function.
void i2s_out_init(uint32_t sample_rate_hz);

// Write @frame_count interleaved stereo 16-bit PCM frames into the ring buffer.
// Returns the number of frames actually accepted (may be less if buffer is full).
size_t i2s_out_write_stereo16(const int16_t *interleaved, size_t frame_count);

// Change the I2S bit-clock rate without stopping playback.
// Adjusts the PIO clock divider to match the new sample rate.
void i2s_out_set_sample_rate(uint32_t sample_rate_hz);

// Returns how many additional stereo frames can be written right now without
// overflowing the ring buffer.
size_t i2s_out_free_frames(void);

// Mark the USB audio interface active/inactive. Disabling streaming drops any
// queued audio so a later stream cannot replay stale samples.
void i2s_out_set_streaming(bool streaming);

// Number of silent frames inserted because streaming data was unavailable.
uint32_t i2s_out_underrun_frames(void);

#endif
