#ifndef AUDIO_BLOCK_QUEUE_H_
#define AUDIO_BLOCK_QUEUE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AUDIO_BLOCK_QUEUE_CAPACITY 6u

typedef struct {
  void *items[AUDIO_BLOCK_QUEUE_CAPACITY];
  uint32_t sample_rate_hz;
  uint32_t buffered_frames;
  uint8_t head;
  uint8_t tail;
  uint8_t count;
  bool streaming;
  bool priming;
  bool underrun_recovery;
} audio_block_queue_t;

void audio_block_queue_reset(audio_block_queue_t *queue, uint32_t sample_rate_hz,
                             bool streaming);
bool audio_block_queue_submit(audio_block_queue_t *queue, void *item, uint16_t frames);
void *audio_block_queue_take(audio_block_queue_t *queue, bool *starved);
void audio_block_queue_complete(audio_block_queue_t *queue, uint16_t frames);
size_t audio_block_queue_drain(audio_block_queue_t *queue, void **items, size_t capacity);
uint32_t audio_block_queue_prime_frames(audio_block_queue_t const *queue);

#endif
