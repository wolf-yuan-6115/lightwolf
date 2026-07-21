#include "audio_feedback.h"

#include <stddef.h>

#define FEEDBACK_FILTER_SHIFT 3u
#define FEEDBACK_CORRECTION_Q16_PER_FRAME 256
#define FEEDBACK_CORRECTION_LIMIT_Q16 32768

void audio_feedback_init(audio_feedback_controller_t *controller, uint32_t sample_rate_hz) {
  if (controller == NULL) return;
  controller->sample_rate_hz = sample_rate_hz;
  controller->target_frames = (sample_rate_hz * 2u + 999u) / 1000u;
  controller->filtered_level_q3 = (int32_t)(controller->target_frames << FEEDBACK_FILTER_SHIFT);
  controller->nominal_q16 = (uint32_t)(((uint64_t)sample_rate_hz << 16u) / 1000u);
}

uint32_t audio_feedback_update(audio_feedback_controller_t *controller, uint32_t buffered_frames) {
  if (controller == NULL || controller->sample_rate_hz == 0u) return 0u;
  int32_t measured_q3 = (int32_t)(buffered_frames << FEEDBACK_FILTER_SHIFT);
  controller->filtered_level_q3 += (measured_q3 - controller->filtered_level_q3) >> FEEDBACK_FILTER_SHIFT;
  int32_t filtered_frames = controller->filtered_level_q3 >> FEEDBACK_FILTER_SHIFT;
  int32_t correction = ((int32_t)controller->target_frames - filtered_frames) *
                       FEEDBACK_CORRECTION_Q16_PER_FRAME;
  if (correction > FEEDBACK_CORRECTION_LIMIT_Q16) correction = FEEDBACK_CORRECTION_LIMIT_Q16;
  if (correction < -FEEDBACK_CORRECTION_LIMIT_Q16) correction = -FEEDBACK_CORRECTION_LIMIT_Q16;
  return (uint32_t)((int32_t)controller->nominal_q16 + correction);
}
