#include <assert.h>
#include <stdint.h>

#include "audio_feedback.h"

int main(void) {
  audio_feedback_controller_t controller;
  audio_feedback_init(&controller, 192000u);
  assert(controller.target_frames == 384u);
  assert(audio_feedback_update(&controller, 384u) == controller.nominal_q16);

  uint32_t low_feedback = audio_feedback_update(&controller, 0u);
  assert(low_feedback > controller.nominal_q16);
  for (uint32_t i = 0u; i < 64u; i++) low_feedback = audio_feedback_update(&controller, 0u);
  assert(low_feedback == controller.nominal_q16 + 32768u);

  audio_feedback_init(&controller, 44100u);
  assert(controller.target_frames == 89u);
  uint32_t high_feedback = controller.nominal_q16;
  for (uint32_t i = 0u; i < 64u; i++) high_feedback = audio_feedback_update(&controller, 1000u);
  assert(high_feedback == controller.nominal_q16 - 32768u);
  return 0;
}
