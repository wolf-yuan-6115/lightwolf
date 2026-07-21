/*
 * Host descriptor-order detection adapted from TinyUSB's uac2_speaker_fb
 * example. Copyright (c) 2024 HiFiPhile, licensed under the MIT License.
 */

#include "quirk_os_guessing.h"

#include "tusb.h"

static tusb_desc_type_t s_descriptor_requests[2];
static uint8_t s_descriptor_request_count = 0u;

static void remember_request(tusb_desc_type_t type) {
  if (s_descriptor_request_count >= 2u) return;
  if (s_descriptor_request_count == 0u || s_descriptor_requests[0] != type) {
    s_descriptor_requests[s_descriptor_request_count++] = type;
  }
}

void quirk_os_guessing_desc_device_cb(void) {
  s_descriptor_request_count = 0u;
}

void quirk_os_guessing_desc_configuration_cb(void) {
  remember_request(TUSB_DESC_CONFIGURATION);
}

void quirk_os_guessing_desc_bos_cb(void) {
  remember_request(TUSB_DESC_BOS);
}

void quirk_os_guessing_desc_string_cb(void) {
  remember_request(TUSB_DESC_STRING);
}

quirk_os_guessing_t quirk_os_guessing_get(void) {
  if (s_descriptor_request_count < 2u) return QUIRK_OS_GUESSING_UNKNOWN;
  if (s_descriptor_requests[0] == TUSB_DESC_BOS &&
      s_descriptor_requests[1] == TUSB_DESC_CONFIGURATION) {
    return QUIRK_OS_GUESSING_LINUX;
  }
  if (s_descriptor_requests[0] == TUSB_DESC_CONFIGURATION &&
      s_descriptor_requests[1] == TUSB_DESC_BOS) {
    return QUIRK_OS_GUESSING_WINDOWS;
  }
  if (s_descriptor_requests[0] == TUSB_DESC_STRING &&
      (s_descriptor_requests[1] == TUSB_DESC_BOS ||
       s_descriptor_requests[1] == TUSB_DESC_CONFIGURATION)) {
    return QUIRK_OS_GUESSING_OSX;
  }
  return QUIRK_OS_GUESSING_UNKNOWN;
}
