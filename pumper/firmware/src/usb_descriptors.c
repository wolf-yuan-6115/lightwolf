// USB descriptor definitions for the Pumper USB DAC.
//
// The device presents itself as a USB Audio Class 2 (UAC2) speaker with:
//   • One Audio Control interface   (Interface 0)
//   • One Audio Streaming interface (Interface 1, alternates 0 and 1)
//   • An isochronous OUT endpoint for audio data from host → device
//   • An isochronous IN  endpoint for SOF feedback (async rate adaptation)
//
// The UAC2 audio graph inside the Audio Control interface:
//
//   [USB Streaming Input Terminal (0x01)]
//          ↓
//   [Feature Unit (0x02) — mute + volume per channel]
//          ↓
//   [Headphones Output Terminal (0x03)]
//          ↑
//   [Internal Clock Source (0x04)] ──────────────────────┘

#include <string.h>

#include "bsp/board_api.h"
#include "quirk_os_guessing.h"
#include "tusb.h"
#include "usb_descriptors.h"

// USB vendor / product IDs assigned to this device.
// VID 0x2E8A is Raspberry Pi's USB VID.
#define USB_VID 0x2E8A
#define USB_PID 0xF10A
#define USB_BCD 0x0101  // Device release number (BCD): 1.01

// Endpoint numbers for audio data and SOF feedback.
// EPNUM_AUDIO_OUT is a host→device (OUT) isochronous endpoint.
// EPNUM_AUDIO_FB  is a device→host (IN)  isochronous feedback endpoint (0x81 = EP1 IN).
enum {
  EPNUM_AUDIO_OUT = 0x01,
  EPNUM_AUDIO_FB = 0x81,
  EPNUM_HID_OUT = 0x02,
  EPNUM_HID_IN = 0x82,
};

// Total byte length of the full Configuration descriptor (all interfaces combined).
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_AUDIO_SPEAKER_STEREO_FB_DESC_LEN + TUD_HID_INOUT_DESC_LEN)

// One vendor-defined collection with fixed 64-byte input and output reports.
// Report IDs are intentionally omitted, so WebHID uses report ID 0.
static uint8_t const desc_hid_report[] = {
    0x06, 0x00, 0xff,  // Usage Page (Vendor 0xff00)
    0x09, 0x01,        // Usage (Pumper EQ Control)
    0xa1, 0x01,        // Collection (Application)
    0x15, 0x00,        // Logical Minimum (0)
    0x26, 0xff, 0x00,  // Logical Maximum (255)
    0x75, 0x08,        // Report Size (8 bits)
    0x95, 0x40,        // Report Count (64 bytes)
    0x09, 0x02,        // Usage (Response)
    0x81, 0x02,        // Input (Data, Variable, Absolute)
    0x09, 0x03,        // Usage (Command)
    0x91, 0x02,        // Output (Data, Variable, Absolute)
    0xc0,              // End Collection
};

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
  (void)instance;
  return desc_hid_report;
}

// Standard USB Device descriptor — identifies the device to the host.
// bDeviceClass = MISC / IAD signals that interface associations are used.
static tusb_desc_device_t const desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0201,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = USB_BCD,
    .iManufacturer = STRID_MANUFACTURER,
    .iProduct = STRID_PRODUCT,
    .iSerialNumber = STRID_SERIAL,
    .bNumConfigurations = 0x01,
};

// TinyUSB callback: return the device descriptor to the host.
uint8_t const *tud_descriptor_device_cb(void) {
  quirk_os_guessing_desc_device_cb();
  return (uint8_t const *) &desc_device;
}

// Full Configuration descriptor blob: config header followed by the complete
// UAC2 speaker descriptor (AC + AS interfaces, endpoints, feedback endpoint).
static uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_AUDIO_SPEAKER_STEREO_FB_DESCRIPTOR(ITF_NUM_AUDIO_CONTROL, STRID_AUDIO_IF,
        CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_RX, CFG_TUD_AUDIO_FUNC_1_RESOLUTION_RX,
        EPNUM_AUDIO_OUT, CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX, EPNUM_AUDIO_FB, 4),
    TUD_HID_INOUT_DESCRIPTOR(ITF_NUM_HID, STRID_HID_IF, HID_ITF_PROTOCOL_NONE, sizeof(desc_hid_report),
        EPNUM_HID_OUT, EPNUM_HID_IN, CFG_TUD_HID_EP_BUFSIZE, 1),
};

// macOS uses the standards-compliant three-byte 10.14 feedback packet at full speed.
static uint8_t const desc_configuration_macos[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_AUDIO_SPEAKER_STEREO_FB_DESCRIPTOR(ITF_NUM_AUDIO_CONTROL, STRID_AUDIO_IF,
        CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_RX, CFG_TUD_AUDIO_FUNC_1_RESOLUTION_RX,
        EPNUM_AUDIO_OUT, CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX, EPNUM_AUDIO_FB, 3),
    TUD_HID_INOUT_DESCRIPTOR(ITF_NUM_HID, STRID_HID_IF, HID_ITF_PROTOCOL_NONE, sizeof(desc_hid_report),
        EPNUM_HID_OUT, EPNUM_HID_IN, CFG_TUD_HID_EP_BUFSIZE, 1),
};

// TinyUSB callback: return the configuration descriptor (index is ignored since
// there is only one configuration).
uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
  (void) index;
  quirk_os_guessing_desc_configuration_cb();
  if (tud_speed_get() == TUSB_SPEED_FULL &&
      quirk_os_guessing_get() == QUIRK_OS_GUESSING_OSX) {
    return desc_configuration_macos;
  }
  return desc_configuration;
}

#define BOS_TOTAL_LEN (TUD_BOS_DESC_LEN + 7u)

static uint8_t const desc_bos[] = {
    TUD_BOS_DESCRIPTOR(BOS_TOTAL_LEN, 1),
    0x07, TUSB_DESC_DEVICE_CAPABILITY, DEVICE_CAPABILITY_USB20_EXTENSION,
    0x00, 0x00, 0x00, 0x00,
};

uint8_t const *tud_descriptor_bos_cb(void) {
  quirk_os_guessing_desc_bos_cb();
  return desc_bos;
}

// String descriptor table indexed by STRID_* constants from usb_descriptors.h.
//   [0] Language ID  (0x0409 = US English)
//   [1] Manufacturer
//   [2] Product
//   [3] Serial number (generated at runtime from board hardware ID)
//   [4] Audio interface name
//   [5] HID control interface name
static char const *string_desc_arr[] = {
    (const char[]) {0x09, 0x04},  // STRID_LANGID: US English (0x0409)
    "LightWolf",                  // STRID_MANUFACTURER
    "Pumper USB DAC",             // STRID_PRODUCT
    NULL,                         // STRID_SERIAL (filled in dynamically)
    "LightWolf Pumper DAC",       // STRID_AUDIO_IF
    "Pumper EQ Control",          // STRID_HID_IF
};

// Temporary buffer used to build UTF-16LE string descriptors on demand.
// The first entry holds the descriptor header; the rest hold the characters.
static uint16_t _desc_str[32 + 1];

// TinyUSB callback: return a string descriptor for the given index.
// Strings are converted from ASCII to USB UTF-16LE on the fly.
uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void) langid;
  quirk_os_guessing_desc_string_cb();
  size_t chr_count;

  switch (index) {
    case STRID_LANGID:
      // Language ID descriptor: two raw bytes copied directly (not a string).
      memcpy(&_desc_str[1], string_desc_arr[0], 2);
      chr_count = 1;
      break;
    case STRID_SERIAL:
      // Read the board's unique hardware serial number into the buffer.
      chr_count = board_usb_get_serial(_desc_str + 1, 32);
      break;
    default: {
      if (index >= TU_ARRAY_SIZE(string_desc_arr)) return NULL;
      char const *str = string_desc_arr[index];
      chr_count = strlen(str);
      if (chr_count > 32) chr_count = 32;
      // Widen each ASCII character to UTF-16LE (safe for ASCII-only strings).
      for (size_t i = 0; i < chr_count; i++) {
        _desc_str[1 + i] = str[i];
      }
      break;
    }
  }

  // Prepend the standard USB string descriptor header:
  //   high byte = descriptor type (TUSB_DESC_STRING = 0x03)
  //   low  byte = total byte length = 2 (header) + 2 bytes per character
  _desc_str[0] = (uint16_t) ((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
  return _desc_str;
}
