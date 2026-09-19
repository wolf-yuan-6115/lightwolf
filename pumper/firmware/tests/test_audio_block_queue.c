#include <assert.h>
#include <stdint.h>

#include "audio_block_queue.h"

int main(void) {
  audio_block_queue_t queue;
  audio_block_queue_reset(&queue, 48000u, true);
  assert(audio_block_queue_prime_frames(&queue) == 96u);
  int blocks[AUDIO_BLOCK_QUEUE_CAPACITY];
  assert(audio_block_queue_submit(&queue, &blocks[0], 48u));
  bool starved = true;
  assert(audio_block_queue_take(&queue, &starved) == NULL && !starved);
  assert(audio_block_queue_submit(&queue, &blocks[1], 48u));
  assert(audio_block_queue_take(&queue, &starved) == &blocks[0] && !starved);
  audio_block_queue_complete(&queue, 48u);
  assert(audio_block_queue_take(&queue, &starved) == &blocks[1] && !starved);
  audio_block_queue_complete(&queue, 48u);
  assert(audio_block_queue_take(&queue, &starved) == NULL && starved);
  assert(queue.priming);
  assert(audio_block_queue_take(&queue, &starved) == NULL && starved);

  assert(audio_block_queue_submit(&queue, &blocks[2], 96u));
  assert(audio_block_queue_take(&queue, &starved) == &blocks[2]);
  audio_block_queue_complete(&queue, 96u);

  for (unsigned i = 0; i < AUDIO_BLOCK_QUEUE_CAPACITY; ++i)
    assert(audio_block_queue_submit(&queue, &blocks[i], 20u));
  assert(!audio_block_queue_submit(&queue, &blocks[0], 20u));
  void *drained[AUDIO_BLOCK_QUEUE_CAPACITY];
  assert(audio_block_queue_drain(&queue, drained, AUDIO_BLOCK_QUEUE_CAPACITY) ==
         AUDIO_BLOCK_QUEUE_CAPACITY);
  assert(queue.count == 0u && queue.buffered_frames == 0u && queue.priming);

  audio_block_queue_reset(&queue, 96000u, false);
  assert(audio_block_queue_prime_frames(&queue) == 192u);
  assert(!audio_block_queue_submit(&queue, &blocks[0], 192u));
  assert(audio_block_queue_take(&queue, &starved) == NULL && !starved);
  return 0;
}
