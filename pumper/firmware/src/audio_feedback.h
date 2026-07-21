#ifndef AUDIO_FEEDBACK_H_
#define AUDIO_FEEDBACK_H_

#include <stdint.h>

typedef struct {
  uint32_t sample_rate_hz;
  int32_t filtered_level_q3;
  uint32_t target_frames;
  uint32_t nominal_q16;
} audio_feedback_controller_t;

void audio_feedback_init(audio_feedback_controller_t *controller, uint32_t sample_rate_hz);
uint32_t audio_feedback_update(audio_feedback_controller_t *controller, uint32_t buffered_frames);

#endif
