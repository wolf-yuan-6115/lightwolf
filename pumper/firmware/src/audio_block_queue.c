#include "audio_block_queue.h"

#include <string.h>

uint32_t audio_block_queue_prime_frames(audio_block_queue_t const *queue) {
  return (queue->sample_rate_hz * 2u + 999u) / 1000u;
}

void audio_block_queue_reset(audio_block_queue_t *queue, uint32_t sample_rate_hz,
                             bool streaming) {
  memset(queue, 0, sizeof(*queue));
  queue->sample_rate_hz = sample_rate_hz;
  queue->streaming = streaming;
  queue->priming = true;
}

bool audio_block_queue_submit(audio_block_queue_t *queue, void *item, uint16_t frames) {
  if (!queue->streaming || !item || queue->count == AUDIO_BLOCK_QUEUE_CAPACITY) return false;
  queue->items[queue->head] = item;
  queue->head = (uint8_t)((queue->head + 1u) % AUDIO_BLOCK_QUEUE_CAPACITY);
  queue->count++;
  queue->buffered_frames += frames;
  return true;
}

void *audio_block_queue_take(audio_block_queue_t *queue, bool *starved) {
  if (starved) *starved = false;
  if (!queue->streaming) return NULL;
  if (queue->priming && queue->buffered_frames >= audio_block_queue_prime_frames(queue))
    queue->priming = queue->underrun_recovery = false;
  if (queue->priming) {
    if (starved) *starved = queue->underrun_recovery;
    return NULL;
  }
  if (!queue->count) {
    queue->priming = true;
    queue->underrun_recovery = true;
    if (starved) *starved = true;
    return NULL;
  }
  void *item = queue->items[queue->tail];
  queue->tail = (uint8_t)((queue->tail + 1u) % AUDIO_BLOCK_QUEUE_CAPACITY);
  queue->count--;
  return item;
}

void audio_block_queue_complete(audio_block_queue_t *queue, uint16_t frames) {
  queue->buffered_frames = frames < queue->buffered_frames ? queue->buffered_frames - frames : 0u;
}

size_t audio_block_queue_drain(audio_block_queue_t *queue, void **items, size_t capacity) {
  size_t count = 0u;
  while (queue->count && count < capacity) {
    items[count++] = queue->items[queue->tail];
    queue->tail = (uint8_t)((queue->tail + 1u) % AUDIO_BLOCK_QUEUE_CAPACITY);
    queue->count--;
  }
  queue->head = queue->tail = 0u;
  queue->buffered_frames = 0u;
  queue->priming = true;
  queue->underrun_recovery = false;
  return count;
}
