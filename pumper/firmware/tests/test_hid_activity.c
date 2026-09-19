#include "hid_activity.h"
#include <assert.h>
#include <stdint.h>
int main(void) {
  hid_activity_t s;
  hid_activity_reset(&s);
  assert(!hid_activity_level(&s, 0, false));
  assert(hid_activity_level(&s, 0, true));
  hid_activity_notify(&s, 100);
  assert(!hid_activity_level(&s, 100, false));
  assert(s.phase == 0 && !s.pending);
  hid_activity_notify(&s, 100);
  assert(!hid_activity_level(&s, 100, true));
  hid_activity_notify(&s, 200);
  hid_activity_notify(&s, 24999);
  assert(!hid_activity_level(&s, 25099, true));
  assert(hid_activity_level(&s, 25100, true));
  assert(hid_activity_level(&s, 50099, true));
  assert(!hid_activity_level(&s, 50100, true));
  assert(hid_activity_level(&s, 75100, true));
  assert(hid_activity_level(&s, 100100, true));
  assert(s.phase == 0);
  // Sustained traffic cannot extend the inverted pulse or eliminate the baseline gap.
  hid_activity_reset(&s);
  for (uint32_t t = 0; t < 1000000; t += 5000) {
    hid_activity_notify(&s, t);
    assert(hid_activity_level(&s, t, true) == ((t / 25000) % 2 != 0));
  }
  hid_activity_reset(&s);
  hid_activity_notify(&s, UINT32_MAX - 20000);
  assert(!hid_activity_level(&s, 4000, true));
  assert(hid_activity_level(&s, 5000, true));
  assert(!hid_activity_level(&s, 5001, false));
  hid_activity_reset(&s);
  assert(!hid_activity_level(&s, 5002, false));
  assert(s.phase == 0 && !s.pending);
  return 0;
}
