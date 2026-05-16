// Feather USB DAC — main application entry point.
//
// This firmware implements a USB Audio Class 2 (UAC2) device on the RP2350.
// The host (PC, phone, …) streams 16-bit stereo PCM audio over USB; the
// firmware forwards it to a connected I2S DAC chip via the PIO-based I2S
// driver (i2s_out.c/audio_i2s.pio).
//
// Features:
//   • Supports 44.1 / 48 / 96 / 192 kHz sample rates (host-selectable)
//   • Per-channel mute and volume control via UAC2 Feature Unit
//   • Link LED indicates active USB audio streaming
//   • Loudness indicator shows audio level (peak VU meter, ~33% max brightness)
//   • -3 dB software attenuation applied before I2S output

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "bsp/board_api.h"
#include "hardware/flash.h"
#include "hardware/pwm.h"
#include "hardware/regs/addressmap.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"
#include "tusb.h"

#include "i2s_out.h"
#include "usb_descriptors.h"

#define AUDIO_CHANNELS   2u   // Stereo: left + right
#define AUDIO_FRAME_BYTES 4u  // 2 bytes/sample × 2 channels = 4 bytes per stereo frame

// GPIO pins for the two status LEDs (active-low, driven via PWM).
#define LINK_LED_PIN            15u  // Lit when USB audio streaming is active
#define LOUDNESS_INDICATOR_PIN  14u  // Brightness tracks the audio peak level

// Volume buttons (active-low with pull-ups).
#define VOLUME_UP_BUTTON_PIN    0u
#define VOLUME_DOWN_BUTTON_PIN  1u

// Jack detection and indicator GPIOs.
// GP10/GP11 are active-low jack detect inputs with pull-ups.
// GP8/GP9 default low and are pulled high when the corresponding jack is present.
// GP13 is the 3.5mm jack indicator and GP12 is the 4.4mm jack indicator.
// Both indicators are active-low (low = sink = LED on), default high.
#define JACK_DETECT_1_PIN 10u
#define JACK_DETECT_2_PIN 11u
#define JACK_CTRL_1_PIN    8u
#define JACK_CTRL_2_PIN    9u
#define JACK_35MM_INDICATOR_PIN 13u
#define JACK_44MM_INDICATOR_PIN 12u

// PWM configuration: 8-bit counter (wrap = 255).
// ON_LEVEL is 10% of full scale so the LED is visible but not blinding.
#define LED_PWM_WRAP      255u
#define LED_PWM_ON_LEVEL  ((LED_PWM_WRAP * 10u) / 100u)
#define LINK_LED_FLASH_ON_MS  90u
#define LINK_LED_FLASH_OFF_MS 90u

#define VOLUME_MIN_PERCENT 0u
#define VOLUME_MAX_PERCENT 100u
#define VOLUME_STEP_PERCENT 10u
#define VOLUME_BUTTON_DEBOUNCE_MS 30u
#define VOLUME_SAVE_DELAY_MS 3000u

#define SETTINGS_FLASH_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define SETTINGS_MAGIC 0x4C575646u // "LWVF"
#define SETTINGS_VERSION 1u

// Sample rates advertised to the host via the Clock Source range descriptor.
// The host picks one and sets it via tud_audio_set_req_entity_cb().
static uint32_t const sample_rates[] = {44100u, 48000u, 96000u, 192000u};
#define N_SAMPLE_RATES TU_ARRAY_SIZE(sample_rates)

// Mute state for master (index 0) and each channel (indices 1 and 2).
// Updated by the host through UAC2 Feature Unit set requests.
static int8_t s_mute[AUDIO_CHANNELS + 1];

// Volume in UAC2 Q8.8 fixed-point dB (0 = 0 dB, -256 = -1 dB).
// Range advertised: -50 dB (–12800) to 0 dB, in 1 dB (256) steps.
static int16_t s_volume_q8[AUDIO_CHANNELS + 1];

// Currently active sample rate; updated when the host sends a SET_CUR request
// to the Clock Source entity.
static uint32_t s_sample_rate_hz = 48000u;

// Byte count of the most recent USB isochronous packet, set in the pre-read
// callback and consumed by audio_task().  Marked volatile because it is written
// from a TinyUSB callback (interrupt context) and read from the main loop.
static volatile uint16_t s_rx_size = 0;

static uint8_t s_output_volume_percent = VOLUME_MAX_PERCENT;
static uint8_t s_saved_volume_percent = VOLUME_MAX_PERCENT;
static bool s_volume_dirty = false;
static uint32_t s_volume_save_deadline_ms = 0;

static bool s_link_led_streaming = false;
static bool s_link_led_flash_active = false;
static bool s_link_led_flash_state = false;
static uint8_t s_link_led_flash_toggles_left = 0;
static uint32_t s_link_led_flash_next_ms = 0;

typedef struct {
  bool stable_pressed;
  bool last_raw_pressed;
  uint32_t last_raw_change_ms;
} button_state_t;

static button_state_t s_vol_up_btn = {0};
static button_state_t s_vol_down_btn = {0};

typedef struct __attribute__((packed)) {
  uint32_t magic;
  uint8_t version;
  uint8_t volume_percent;
  uint8_t reserved0;
  uint8_t reserved1;
  uint32_t checksum;
} persisted_settings_t;

static void led_set(uint pin, bool on);

static uint32_t settings_checksum(uint8_t volume_percent) {
  return SETTINGS_MAGIC ^ ((uint32_t) SETTINGS_VERSION << 24u) ^ ((uint32_t) volume_percent << 8u) ^ 0xA55AA55Au;
}

static void led_set_link(bool on) {
  led_set(LINK_LED_PIN, on);
}

static void link_led_apply_current_state(void) {
  if (s_link_led_flash_active) {
    led_set_link(s_link_led_flash_state);
  } else {
    led_set_link(s_link_led_streaming);
  }
}

static void link_led_start_flash(uint8_t flashes) {
  if (flashes == 0) {
    return;
  }
  s_link_led_flash_active = true;
  s_link_led_flash_state = true;
  s_link_led_flash_toggles_left = (uint8_t) (flashes * 2u - 1u); // Start ON immediately, then toggle remaining edges.
  s_link_led_flash_next_ms = to_ms_since_boot(get_absolute_time()) + LINK_LED_FLASH_ON_MS;
  link_led_apply_current_state();
}

static void link_led_task(void) {
  if (!s_link_led_flash_active) {
    return;
  }
  uint32_t now_ms = to_ms_since_boot(get_absolute_time());
  if ((int32_t) (now_ms - s_link_led_flash_next_ms) < 0) {
    return;
  }

  s_link_led_flash_state = !s_link_led_flash_state;
  if (s_link_led_flash_toggles_left > 0) {
    s_link_led_flash_toggles_left--;
  }
  if (s_link_led_flash_toggles_left == 0u) {
    s_link_led_flash_active = false;
    link_led_apply_current_state();
    return;
  }

  s_link_led_flash_next_ms = now_ms + (s_link_led_flash_state ? LINK_LED_FLASH_ON_MS : LINK_LED_FLASH_OFF_MS);
  link_led_apply_current_state();
}

static bool button_poll_pressed(button_state_t *state, bool raw_pressed, uint32_t now_ms) {
  if (raw_pressed != state->last_raw_pressed) {
    state->last_raw_pressed = raw_pressed;
    state->last_raw_change_ms = now_ms;
  }
  if ((int32_t) (now_ms - state->last_raw_change_ms) >= (int32_t) VOLUME_BUTTON_DEBOUNCE_MS &&
      state->stable_pressed != state->last_raw_pressed) {
    state->stable_pressed = state->last_raw_pressed;
    return state->stable_pressed;
  }
  return false;
}

static bool load_persisted_settings(void) {
  persisted_settings_t const *stored = (persisted_settings_t const *) (XIP_BASE + SETTINGS_FLASH_OFFSET);
  if (stored->magic != SETTINGS_MAGIC || stored->version != SETTINGS_VERSION) {
    return false;
  }
  if (stored->volume_percent > VOLUME_MAX_PERCENT) {
    return false;
  }
  if (stored->checksum != settings_checksum(stored->volume_percent)) {
    return false;
  }

  s_output_volume_percent = stored->volume_percent;
  s_saved_volume_percent = stored->volume_percent;
  return true;
}

static void save_persisted_settings(void) {
  uint8_t page_buf[FLASH_PAGE_SIZE];
  memset(page_buf, 0xFF, sizeof(page_buf));

  persisted_settings_t settings = {
      .magic = SETTINGS_MAGIC,
      .version = SETTINGS_VERSION,
      .volume_percent = s_output_volume_percent,
      .reserved0 = 0,
      .reserved1 = 0,
      .checksum = settings_checksum(s_output_volume_percent),
  };
  memcpy(page_buf, &settings, sizeof(settings));

  uint32_t irq_state = save_and_disable_interrupts();
  flash_range_erase(SETTINGS_FLASH_OFFSET, FLASH_SECTOR_SIZE);
  flash_range_program(SETTINGS_FLASH_OFFSET, page_buf, FLASH_PAGE_SIZE);
  restore_interrupts(irq_state);

  s_saved_volume_percent = s_output_volume_percent;
}

static void volume_buttons_init(void) {
  gpio_init(VOLUME_UP_BUTTON_PIN);
  gpio_set_dir(VOLUME_UP_BUTTON_PIN, GPIO_IN);
  gpio_pull_up(VOLUME_UP_BUTTON_PIN);
  s_vol_up_btn.last_raw_pressed = !gpio_get(VOLUME_UP_BUTTON_PIN);
  s_vol_up_btn.stable_pressed = s_vol_up_btn.last_raw_pressed;
  s_vol_up_btn.last_raw_change_ms = to_ms_since_boot(get_absolute_time());

  gpio_init(VOLUME_DOWN_BUTTON_PIN);
  gpio_set_dir(VOLUME_DOWN_BUTTON_PIN, GPIO_IN);
  gpio_pull_up(VOLUME_DOWN_BUTTON_PIN);
  s_vol_down_btn.last_raw_pressed = !gpio_get(VOLUME_DOWN_BUTTON_PIN);
  s_vol_down_btn.stable_pressed = s_vol_down_btn.last_raw_pressed;
  s_vol_down_btn.last_raw_change_ms = to_ms_since_boot(get_absolute_time());
}

static void apply_volume_step(int8_t delta_percent) {
  int16_t next = (int16_t) s_output_volume_percent + delta_percent;
  if (next < (int16_t) VOLUME_MIN_PERCENT) {
    next = (int16_t) VOLUME_MIN_PERCENT;
  } else if (next > (int16_t) VOLUME_MAX_PERCENT) {
    next = (int16_t) VOLUME_MAX_PERCENT;
  }

  if ((uint8_t) next == s_output_volume_percent) {
    link_led_start_flash(2);
    return;
  }

  s_output_volume_percent = (uint8_t) next;
  s_volume_dirty = true;
  s_volume_save_deadline_ms = to_ms_since_boot(get_absolute_time()) + VOLUME_SAVE_DELAY_MS;
  link_led_start_flash(1);
}

static void volume_buttons_task(void) {
  uint32_t now_ms = to_ms_since_boot(get_absolute_time());

  if (button_poll_pressed(&s_vol_up_btn, !gpio_get(VOLUME_UP_BUTTON_PIN), now_ms)) {
    apply_volume_step((int8_t) VOLUME_STEP_PERCENT);
  }
  if (button_poll_pressed(&s_vol_down_btn, !gpio_get(VOLUME_DOWN_BUTTON_PIN), now_ms)) {
    apply_volume_step(-(int8_t) VOLUME_STEP_PERCENT);
  }

  if (s_volume_dirty && (int32_t) (now_ms - s_volume_save_deadline_ms) >= 0) {
    if (s_output_volume_percent != s_saved_volume_percent) {
      save_persisted_settings();
    }
    s_volume_dirty = false;
  }

  link_led_task();
}

// Initialise a GPIO pin as a PWM-driven LED output.
// The PWM counter runs 0–255; polarity is inverted because the LEDs are
// active-low (connected between the GPIO and VCC).
static void led_pwm_init(uint pin) {
  gpio_set_function(pin, GPIO_FUNC_PWM);
  uint slice = pwm_gpio_to_slice_num(pin);
  uint channel = pwm_gpio_to_channel(pin);

  pwm_config cfg = pwm_get_default_config();
  pwm_config_set_wrap(&cfg, LED_PWM_WRAP);
  pwm_init(slice, &cfg, false);

  // Board LEDs are active-low: invert PWM so level 0 = fully off (pin high).
  bool invert_a = (channel == PWM_CHAN_A);
  bool invert_b = (channel == PWM_CHAN_B);
  pwm_set_output_polarity(slice, invert_a, invert_b);
  pwm_set_chan_level(slice, channel, 0);
  pwm_set_enabled(slice, true);
}

// Set a LED to its fixed on-level or fully off.
static void led_set(uint pin, bool on) {
  uint slice = pwm_gpio_to_slice_num(pin);
  uint channel = pwm_gpio_to_channel(pin);
  pwm_set_chan_level(slice, channel, on ? LED_PWM_ON_LEVEL : 0);
}

// Set a LED to an arbitrary brightness level (0 = off, LED_PWM_WRAP = full).
static void led_set_level(uint pin, uint16_t level) {
  uint slice = pwm_gpio_to_slice_num(pin);
  uint channel = pwm_gpio_to_channel(pin);
  if (level > LED_PWM_WRAP) level = LED_PWM_WRAP;
  pwm_set_chan_level(slice, channel, level);
}

// Configure jack-detection IO defaults.
static void jack_detection_init(void) {
  gpio_init(JACK_DETECT_1_PIN);
  gpio_set_dir(JACK_DETECT_1_PIN, GPIO_IN);
  gpio_pull_up(JACK_DETECT_1_PIN);

  gpio_init(JACK_DETECT_2_PIN);
  gpio_set_dir(JACK_DETECT_2_PIN, GPIO_IN);
  gpio_pull_up(JACK_DETECT_2_PIN);

  gpio_init(JACK_CTRL_1_PIN);
  gpio_set_dir(JACK_CTRL_1_PIN, GPIO_OUT);
  gpio_put(JACK_CTRL_1_PIN, 0);
  gpio_pull_down(JACK_CTRL_1_PIN);

  gpio_init(JACK_CTRL_2_PIN);
  gpio_set_dir(JACK_CTRL_2_PIN, GPIO_OUT);
  gpio_put(JACK_CTRL_2_PIN, 0);
  gpio_pull_down(JACK_CTRL_2_PIN);

  gpio_init(JACK_35MM_INDICATOR_PIN);
  gpio_set_dir(JACK_35MM_INDICATOR_PIN, GPIO_OUT);
  gpio_put(JACK_35MM_INDICATOR_PIN, 1);

  gpio_init(JACK_44MM_INDICATOR_PIN);
  gpio_set_dir(JACK_44MM_INDICATOR_PIN, GPIO_OUT);
  gpio_put(JACK_44MM_INDICATOR_PIN, 1);
}

// Poll jack-detect inputs and drive the associated control/indicator pins.
static void jack_detection_task(void) {
  bool const jack_1_present = !gpio_get(JACK_DETECT_1_PIN);
  bool const jack_2_present = !gpio_get(JACK_DETECT_2_PIN);

  gpio_put(JACK_CTRL_1_PIN, jack_1_present ? 1 : 0);
  gpio_put(JACK_35MM_INDICATOR_PIN, jack_1_present ? 0 : 1);

  gpio_put(JACK_CTRL_2_PIN, jack_2_present ? 1 : 0);
  gpio_put(JACK_44MM_INDICATOR_PIN, jack_2_present ? 0 : 1);
}

// Respond to an unhandled control request with a zero-filled buffer.
// Used as a safe fallback for GET requests on unknown entities.
static bool send_zero_control(uint8_t rhport, tusb_control_request_t const *p_request) {
  static uint8_t const zero_buf[64] = {0};
  uint16_t len = tu_le16toh(p_request->wLength);
  if (len > sizeof(zero_buf)) len = sizeof(zero_buf);
  return tud_control_xfer(rhport, p_request, (void *) zero_buf, len);
}

// Apply a host-requested sample rate change and reconfigure the I2S bit clock.
static void handle_sample_rate_change(uint32_t new_rate_hz) {
  if (new_rate_hz == 0) return;
  s_sample_rate_hz = new_rate_hz;
  i2s_out_set_sample_rate(s_sample_rate_hz);
}

// Main audio processing task — called every iteration of the main loop.
//
// Drains received USB audio data from the TinyUSB software receive buffer,
// applies peak-level metering to drive the loudness indicator, attenuates the signal
// by -3 dB, then forwards the samples to the I2S ring buffer.
//
// The function processes at most one buffer's worth of data per call, limited
// by s_rx_size (set by the USB receive callback) and the space available in
// the I2S ring buffer.
static void audio_task(void) {
  int16_t sample_buf[CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX / sizeof(int16_t)];

  while (s_rx_size >= AUDIO_FRAME_BYTES) {
    size_t free_frames = i2s_out_free_frames();
    if (free_frames == 0) break;

    uint16_t to_read = s_rx_size;
    if (to_read > sizeof(sample_buf)) to_read = sizeof(sample_buf);
    to_read = (uint16_t) (to_read & ~(AUDIO_FRAME_BYTES - 1u));
    if (to_read == 0) break;

    uint16_t got = tud_audio_read(sample_buf, to_read);
    uint16_t frames = (uint16_t) (got / AUDIO_FRAME_BYTES);

    if (got != 0) {
      // Calculate audio level (peak) from original samples for loudness indicator.
      uint32_t peak = 0;
      uint16_t num_samples = got / sizeof(int16_t);
      for (uint16_t i = 0; i < num_samples; i++) {
        uint32_t abs_sample = (sample_buf[i] < 0) ? (uint32_t)(-sample_buf[i]) : (uint32_t)sample_buf[i];
        if (abs_sample > peak) peak = abs_sample;
      }
      
      // Map peak (0-32768) to indicator brightness at ~33% (0-85) for reduced brightness.
      uint16_t led_level = (uint16_t)((peak * LED_PWM_WRAP) / (32768u * 3u));
      led_set_level(LOUDNESS_INDICATOR_PIN, led_level);
      
      // Apply -3dB attenuation and button-controlled output volume.
      for (uint16_t i = 0; i < num_samples; i++) {
        int32_t scaled = (int32_t) sample_buf[i] * 181 * (int32_t) s_output_volume_percent;
        scaled /= (256 * 100);
        if (scaled > 32767) scaled = 32767;
        if (scaled < -32768) scaled = -32768;
        sample_buf[i] = (int16_t) scaled;
      }
    }

    size_t accepted = i2s_out_write_stereo16(sample_buf, frames);

    if (accepted < frames) break;
    s_rx_size = 0;
  }
}

// Handle UAC2 GET requests directed at the Clock Source entity.
//
// Supported control selectors:
//   AUDIO_CS_CTRL_SAM_FREQ  — return the current sample rate (CUR) or the list
//                             of supported rates (RANGE).
//   AUDIO_CS_CTRL_CLK_VALID — always report clock as valid (1).
//
// The sample frequency can be encoded as a 3-byte little-endian value (some
// older UAC1-style hosts) or as a standard 4-byte UAC2 integer.
static bool tud_audio_clock_get_request(uint8_t rhport, audio_control_request_t const *request) {
  uint16_t const w_length = tu_le16toh(((tusb_control_request_t const *) request)->wLength);

  if (request->bControlSelector == AUDIO_CS_CTRL_SAM_FREQ) {
    if (request->bRequest == AUDIO_CS_REQ_CUR) {
      if (w_length == 3) {
        uint8_t freq[3] = {
            (uint8_t) (s_sample_rate_hz & 0xffu),
            (uint8_t) ((s_sample_rate_hz >> 8u) & 0xffu),
            (uint8_t) ((s_sample_rate_hz >> 16u) & 0xffu),
        };
        return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *) request, freq, sizeof(freq));
      }
      audio_control_cur_4_t curf = { (int32_t) s_sample_rate_hz };
      return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *) request, &curf, sizeof(curf));
    } else if (request->bRequest == AUDIO_CS_REQ_RANGE) {
      audio_control_range_4_n_t(N_SAMPLE_RATES) rangef = {
          .wNumSubRanges = tu_htole16(N_SAMPLE_RATES),
      };
      for (uint8_t i = 0; i < N_SAMPLE_RATES; i++) {
        rangef.subrange[i].bMin = (int32_t) sample_rates[i];
        rangef.subrange[i].bMax = (int32_t) sample_rates[i];
        rangef.subrange[i].bRes = 0;
      }
      return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *) request, &rangef, sizeof(rangef));
    }
  } else if (request->bControlSelector == AUDIO_CS_CTRL_CLK_VALID && request->bRequest == AUDIO_CS_REQ_RANGE) {
    audio_control_range_1_n_t(1) range_valid = {
        .wNumSubRanges = tu_htole16(1),
        .subrange[0] = { .bMin = 1, .bMax = 1, .bRes = 0 },
    };
    return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *) request, &range_valid, sizeof(range_valid));
  } else if (request->bControlSelector == AUDIO_CS_CTRL_CLK_VALID && request->bRequest == AUDIO_CS_REQ_CUR) {
    audio_control_cur_1_t cur_valid = {.bCur = 1};
    return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *) request, &cur_valid, sizeof(cur_valid));
  }
  return false;
}

// Handle UAC2 GET requests directed at the Feature Unit (mute + volume).
//
// MUTE CUR   — return current mute state for the requested channel.
// VOLUME CUR — return current volume in Q8.8 dB for the requested channel.
// VOLUME RANGE — return the supported volume range: -50 dB to 0 dB, 1 dB steps.
static bool tud_audio_feature_unit_get_request(uint8_t rhport, audio_control_request_t const *request) {
  if (request->bControlSelector == AUDIO_FU_CTRL_MUTE && request->bRequest == AUDIO_CS_REQ_CUR) {
    audio_control_cur_1_t mute1 = {.bCur = (uint8_t) s_mute[request->bChannelNumber]};
    return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *) request, &mute1, sizeof(mute1));
  }

  if (request->bControlSelector == AUDIO_FU_CTRL_VOLUME) {
    if (request->bRequest == AUDIO_CS_REQ_RANGE) {
      audio_control_range_2_n_t(1) range_vol = {
          .wNumSubRanges = tu_htole16(1),
          .subrange[0] = { .bMin = tu_htole16(-12800), .bMax = tu_htole16(0), .bRes = tu_htole16(256) },
      };
      return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *) request, &range_vol, sizeof(range_vol));
    } else if (request->bRequest == AUDIO_CS_REQ_CUR) {
      audio_control_cur_2_t cur_vol = {.bCur = tu_htole16(s_volume_q8[request->bChannelNumber])};
      return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *) request, &cur_vol, sizeof(cur_vol));
    }
  }
  return false;
}

// Handle UAC2 SET requests directed at the Feature Unit.
// Only SET_CUR is supported; updates s_mute[] or s_volume_q8[] accordingly.
// Note: volume is stored but not yet applied in software (the I2S DAC chip
// handles volume independently via its own control interface).
static bool tud_audio_feature_unit_set_request(audio_control_request_t const *request, uint8_t const *buf) {
  TU_VERIFY(request->bRequest == AUDIO_CS_REQ_CUR);

  if (request->bControlSelector == AUDIO_FU_CTRL_MUTE) {
    TU_VERIFY(request->wLength == sizeof(audio_control_cur_1_t));
    s_mute[request->bChannelNumber] = ((audio_control_cur_1_t const *) buf)->bCur;
    return true;
  } else if (request->bControlSelector == AUDIO_FU_CTRL_VOLUME) {
    TU_VERIFY(request->wLength == sizeof(audio_control_cur_2_t));
    s_volume_q8[request->bChannelNumber] = ((audio_control_cur_2_t const *) buf)->bCur;
    return true;
  }
  return false;
}

// TinyUSB callback: dispatch GET control requests to the appropriate entity handler.
bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
  audio_control_request_t const *request = (audio_control_request_t const *) p_request;
  if (request->bEntityID == UAC2_ENTITY_CLOCK) return tud_audio_clock_get_request(rhport, request);
  if (request->bEntityID == UAC2_ENTITY_FEATURE_UNIT) return tud_audio_feature_unit_get_request(rhport, request);
  return send_zero_control(rhport, p_request);
}

// TinyUSB callback: dispatch SET control requests to the appropriate entity handler.
// Handles sample-rate changes on the Clock entity and mute/volume on the Feature Unit.
// Both 3-byte (legacy UAC1-style) and 4-byte (UAC2) sample-rate encodings are accepted.
bool tud_audio_set_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *buf) {
  (void) rhport;
  audio_control_request_t const *request = (audio_control_request_t const *) p_request;

  if (request->bEntityID == UAC2_ENTITY_CLOCK && request->bControlSelector == AUDIO_CS_CTRL_SAM_FREQ &&
      request->bRequest == AUDIO_CS_REQ_CUR) {
    uint16_t const w_length = tu_le16toh(p_request->wLength);
    if (w_length == 3) {
      uint32_t rate = (uint32_t) buf[0] | ((uint32_t) buf[1] << 8u) | ((uint32_t) buf[2] << 16u);
      handle_sample_rate_change(rate);
      return true;
    }
    if (w_length == sizeof(audio_control_cur_4_t)) {
      handle_sample_rate_change((uint32_t) ((audio_control_cur_4_t const *) buf)->bCur);
      return true;
    }
  }

  if (request->bEntityID == UAC2_ENTITY_FEATURE_UNIT) return tud_audio_feature_unit_set_request(request, buf);
  return true;
}

// TinyUSB callback: called when the host closes the audio streaming interface
// (switches to alternate setting 0). Turn off the link LED.
bool tud_audio_set_itf_close_EP_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
  (void) rhport;
  uint8_t const itf = tu_u16_low(tu_le16toh(p_request->wIndex));
  uint8_t const alt = tu_u16_low(tu_le16toh(p_request->wValue));
  if (itf == ITF_NUM_AUDIO_STREAMING && alt == 0) {
    s_link_led_streaming = false;
    link_led_apply_current_state();
  }
  return true;
}

// TinyUSB callback: called when the host selects an alternate setting on the
// audio streaming interface.  Alternate 0 = idle (no bandwidth), alternate 1 =
// active streaming. Turn the link LED on/off accordingly and reset rx counter.
bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
  (void) rhport;
  uint8_t const itf = tu_u16_low(tu_le16toh(p_request->wIndex));
  uint8_t const alt = tu_u16_low(tu_le16toh(p_request->wValue));
  if (itf == ITF_NUM_AUDIO_STREAMING) {
    s_link_led_streaming = (alt != 0);
    link_led_apply_current_state();
    s_rx_size = 0;
  }
  return true;
}

// TinyUSB callback: called just before audio data is read from the USB buffer.
// Records the number of bytes received so audio_task() knows how much to drain.
bool tud_audio_rx_done_pre_read_cb(uint8_t rhport, uint16_t n_bytes_received, uint8_t func_id, uint8_t ep_out, uint8_t cur_alt_setting) {
  (void) rhport;
  (void) func_id;
  (void) ep_out;
  (void) cur_alt_setting;
  s_rx_size = n_bytes_received;
  return true;
}

// TinyUSB callback: SET control request on the audio data endpoint.
// Some hosts (especially older ones) set the sample rate via the endpoint
// rather than the Clock Source entity.  Both 3-byte and 4-byte encodings
// are handled for maximum compatibility.
bool tud_audio_set_req_ep_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *pBuff) {
  (void) rhport;
  if (p_request->bRequest == AUDIO_CS_REQ_CUR) {
    if (p_request->wLength == 3) {
      uint32_t rate = (uint32_t) pBuff[0] | ((uint32_t) pBuff[1] << 8u) | ((uint32_t) pBuff[2] << 16u);
      handle_sample_rate_change(rate);
      return true;
    }
    if (p_request->wLength == 4) {
      handle_sample_rate_change((uint32_t) ((audio_control_cur_4_t const *) pBuff)->bCur);
      return true;
    }
  }
  return false;
}

// TinyUSB callback: GET control request on the audio data endpoint.
// Returns the current sample rate (CUR) or the list of supported rates (RANGE).
bool tud_audio_get_req_ep_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
  uint16_t const w_length = tu_le16toh(p_request->wLength);

  if (p_request->bRequest == AUDIO_CS_REQ_CUR) {
    if (w_length == 3) {
      uint8_t freq[3] = {
          (uint8_t) (s_sample_rate_hz & 0xffu),
          (uint8_t) ((s_sample_rate_hz >> 8u) & 0xffu),
          (uint8_t) ((s_sample_rate_hz >> 16u) & 0xffu),
      };
      return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, freq, sizeof(freq));
    }
    if (w_length == 4) {
      audio_control_cur_4_t curf = { (int32_t) s_sample_rate_hz };
      return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &curf, sizeof(curf));
    }
  } else if (p_request->bRequest == AUDIO_CS_REQ_RANGE) {
    audio_control_range_4_n_t(N_SAMPLE_RATES) rangef = {
        .wNumSubRanges = tu_htole16(N_SAMPLE_RATES),
    };
    for (uint8_t i = 0; i < N_SAMPLE_RATES; i++) {
      rangef.subrange[i].bMin = (int32_t) sample_rates[i];
      rangef.subrange[i].bMax = (int32_t) sample_rates[i];
      rangef.subrange[i].bRes = 0;
    }
    return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &rangef, sizeof(rangef));
  }
  return send_zero_control(rhport, p_request);
}

// TinyUSB callback: provide SOF feedback parameters for asynchronous rate adaptation.
// FREQUENCY_FIXED tells TinyUSB to send a fixed-frequency feedback value derived
// from the nominal sample rate rather than measuring the actual I2S clock.
void tud_audio_feedback_params_cb(uint8_t func_id, uint8_t alt_itf, audio_feedback_params_t *feedback_param) {
  (void) func_id;
  (void) alt_itf;
  feedback_param->method = AUDIO_FEEDBACK_METHOD_FREQUENCY_FIXED;
  feedback_param->sample_freq = s_sample_rate_hz;
}

int main(void) {
  board_init();

  load_persisted_settings();
  jack_detection_init();
  volume_buttons_init();

  // Initialise both status LEDs and ensure they start off.
  led_pwm_init(LINK_LED_PIN);
  led_pwm_init(LOUDNESS_INDICATOR_PIN);
  s_link_led_streaming = false;
  link_led_apply_current_state();
  led_set(LOUDNESS_INDICATOR_PIN, false);

  // Start the I2S output driver (plays silence until USB audio arrives).
  i2s_out_init(s_sample_rate_hz);

  // Initialise the TinyUSB device stack and any post-TinyUSB board peripherals.
  tud_init(BOARD_TUD_RHPORT);
  board_init_after_tusb();

  // Clear mute and volume state (no mute, 0 dB).
  memset(s_mute, 0, sizeof(s_mute));
  memset(s_volume_q8, 0, sizeof(s_volume_q8));

  // Main loop: process USB events then forward any received audio to I2S.
  while (true) {
    tud_task();    // TinyUSB internal event pump (must be called regularly)
    audio_task();  // Drain USB receive buffer → I2S ring buffer
    jack_detection_task();
    volume_buttons_task();
  }
}
