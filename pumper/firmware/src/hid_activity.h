#ifndef HID_ACTIVITY_H
#define HID_ACTIVITY_H
#include <stdbool.h>
#include <stdint.h>
typedef struct {
  uint32_t started;
  uint8_t phase;
  bool pending;
} hid_activity_t;
void hid_activity_reset(hid_activity_t *s);
void hid_activity_notify(hid_activity_t *s, uint32_t now);
bool hid_activity_level(hid_activity_t *s, uint32_t now, bool streaming);
#endif
