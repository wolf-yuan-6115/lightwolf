#ifndef I2S_OUT_H_
#define I2S_OUT_H_

#include "audio_format.h"
#include "audio_block_queue.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define I2S_AUDIO_BLOCK_COUNT AUDIO_BLOCK_QUEUE_CAPACITY
#define I2S_AUDIO_BLOCK_MAX_FRAMES 194u

typedef struct __attribute__((aligned(16))) {
  uint16_t frames;
  uint16_t word_count;
  uint32_t stream_generation;
  audio_sample_format_t format;
  union {
    float samples[I2S_AUDIO_BLOCK_MAX_FRAMES * 2u];
    uint32_t words[I2S_AUDIO_BLOCK_MAX_FRAMES * 2u];
  } data;
} i2s_audio_block_t;

typedef void (*i2s_block_release_fn)(i2s_audio_block_t *block);

void i2s_out_init(uint32_t sample_rate_hz, audio_sample_format_t format,
                  i2s_block_release_fn release);
void i2s_out_set_format(uint32_t sample_rate_hz, audio_sample_format_t format);
bool i2s_out_submit(i2s_audio_block_t *block);
void i2s_out_set_streaming(bool streaming);
uint32_t i2s_out_buffered_frames(void);
uint32_t i2s_out_underrun_frames(void);
uint32_t i2s_out_low_water_frames(void);

#endif
