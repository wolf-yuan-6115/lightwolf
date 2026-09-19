#include "hid_activity.h"
#define PULSE_US 25000u
void hid_activity_reset(hid_activity_t *s) { *s = (hid_activity_t){0}; }
void hid_activity_notify(hid_activity_t *s, uint32_t now) {
  if (!s->phase) {
    s->phase = 1;
    s->started = now;
  } else
    s->pending = true;
}
bool hid_activity_level(hid_activity_t *s, uint32_t now, bool streaming) {
  if (!streaming) {
    hid_activity_reset(s);
    return false;
  }
  if (s->phase && (uint32_t)(now - s->started) >= PULSE_US) {
    if (s->phase == 1) {
      s->phase = 2;
      s->started = now;
    } else if (s->pending) {
      s->phase = 1;
      s->pending = false;
      s->started = now;
    } else
      s->phase = 0;
  }
  return s->phase != 1;
}
