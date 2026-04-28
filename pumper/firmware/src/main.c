// Pumper USB DAC — main application entry point.
//
// This firmware implements a USB Audio Class 2 (UAC2) device on the RP2040/
// RP2350.  The host (PC, phone, …) streams 16-bit stereo PCM audio over USB;
// the firmware forwards it to a connected I2S DAC chip via the PIO-based I2S
// driver (i2s_out.c/audio_i2s.pio).
//
// Features:
//   • Supports 44.1 / 48 / 96 / 192 kHz sample rates (host-selectable)
//   • Per-channel mute and volume control via UAC2 Feature Unit
//   • Red LED indicates active USB audio streaming
//   • Blue LED shows audio level (peak VU meter, ~33% max brightness)
//   • -3 dB software attenuation applied before I2S output

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "bsp/board_api.h"
#include "hardware/pwm.h"
#include "pico/stdlib.h"
#include "tusb.h"

#include "i2s_out.h"
#include "usb_descriptors.h"

#define AUDIO_CHANNELS   2u   // Stereo: left + right
#define AUDIO_FRAME_BYTES 4u  // 2 bytes/sample × 2 channels = 4 bytes per stereo frame

// GPIO pins for the two status LEDs (active-low, driven via PWM).
#define LED_RED_PIN  10u  // Lit when USB audio streaming is active
#define LED_BLUE_PIN  9u  // Brightness tracks the audio peak level

// PWM configuration: 8-bit counter (wrap = 255).
// ON_LEVEL is 10% of full scale so the LED is visible but not blinding.
#define LED_PWM_WRAP      255u
#define LED_PWM_ON_LEVEL  ((LED_PWM_WRAP * 10u) / 100u)

static uint32_t const sample_rates[] = {44100u, 48000u, 96000u, 192000u};
#define N_SAMPLE_RATES TU_ARRAY_SIZE(sample_rates)

static int8_t s_mute[AUDIO_CHANNELS + 1];
static int16_t s_volume_q8[AUDIO_CHANNELS + 1];
static uint32_t s_sample_rate_hz = 48000u;
static volatile uint16_t s_rx_size = 0;

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

static void led_set(uint pin, bool on) {
  uint slice = pwm_gpio_to_slice_num(pin);
  uint channel = pwm_gpio_to_channel(pin);
  pwm_set_chan_level(slice, channel, on ? LED_PWM_ON_LEVEL : 0);
}

static void led_set_level(uint pin, uint16_t level) {
  uint slice = pwm_gpio_to_slice_num(pin);
  uint channel = pwm_gpio_to_channel(pin);
  if (level > LED_PWM_WRAP) level = LED_PWM_WRAP;
  pwm_set_chan_level(slice, channel, level);
}

static bool send_zero_control(uint8_t rhport, tusb_control_request_t const *p_request) {
  static uint8_t const zero_buf[64] = {0};
  uint16_t len = tu_le16toh(p_request->wLength);
  if (len > sizeof(zero_buf)) len = sizeof(zero_buf);
  return tud_control_xfer(rhport, p_request, (void *) zero_buf, len);
}

static void handle_sample_rate_change(uint32_t new_rate_hz) {
  if (new_rate_hz == 0) return;
  s_sample_rate_hz = new_rate_hz;
  i2s_out_set_sample_rate(s_sample_rate_hz);
}

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
      // Calculate audio level (peak) from original samples for LED
      uint32_t peak = 0;
      uint16_t num_samples = got / sizeof(int16_t);
      for (uint16_t i = 0; i < num_samples; i++) {
        uint32_t abs_sample = (sample_buf[i] < 0) ? (uint32_t)(-sample_buf[i]) : (uint32_t)sample_buf[i];
        if (abs_sample > peak) peak = abs_sample;
      }
      
      // Map peak (0-32768) to LED brightness at ~33% (0-85) for reduced brightness
      uint16_t led_level = (uint16_t)((peak * LED_PWM_WRAP) / (32768u * 3u));
      led_set_level(LED_BLUE_PIN, led_level);
      
      // Apply -3dB attenuation to samples for I2S output: multiply by ~0.707 (181/256)
      for (uint16_t i = 0; i < num_samples; i++) {
        sample_buf[i] = (int16_t)((sample_buf[i] * 181) / 256);
      }
    }

    size_t accepted = i2s_out_write_stereo16(sample_buf, frames);

    if (accepted < frames) break;
    s_rx_size = 0;
  }
}

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

bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
  audio_control_request_t const *request = (audio_control_request_t const *) p_request;
  if (request->bEntityID == UAC2_ENTITY_CLOCK) return tud_audio_clock_get_request(rhport, request);
  if (request->bEntityID == UAC2_ENTITY_FEATURE_UNIT) return tud_audio_feature_unit_get_request(rhport, request);
  return send_zero_control(rhport, p_request);
}

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

bool tud_audio_set_itf_close_EP_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
  (void) rhport;
  uint8_t const itf = tu_u16_low(tu_le16toh(p_request->wIndex));
  uint8_t const alt = tu_u16_low(tu_le16toh(p_request->wValue));
  if (itf == ITF_NUM_AUDIO_STREAMING && alt == 0) led_set(LED_RED_PIN, false);
  return true;
}

bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
  (void) rhport;
  uint8_t const itf = tu_u16_low(tu_le16toh(p_request->wIndex));
  uint8_t const alt = tu_u16_low(tu_le16toh(p_request->wValue));
  if (itf == ITF_NUM_AUDIO_STREAMING) {
    led_set(LED_RED_PIN, alt != 0);
    s_rx_size = 0;
  }
  return true;
}

bool tud_audio_rx_done_pre_read_cb(uint8_t rhport, uint16_t n_bytes_received, uint8_t func_id, uint8_t ep_out, uint8_t cur_alt_setting) {
  (void) rhport;
  (void) func_id;
  (void) ep_out;
  (void) cur_alt_setting;
  s_rx_size = n_bytes_received;
  return true;
}

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

void tud_audio_feedback_params_cb(uint8_t func_id, uint8_t alt_itf, audio_feedback_params_t *feedback_param) {
  (void) func_id;
  (void) alt_itf;
  feedback_param->method = AUDIO_FEEDBACK_METHOD_FREQUENCY_FIXED;
  feedback_param->sample_freq = s_sample_rate_hz;
}

int main(void) {
  board_init();

  led_pwm_init(LED_RED_PIN);
  led_pwm_init(LED_BLUE_PIN);
  led_set(LED_RED_PIN, false);
  led_set(LED_BLUE_PIN, false);

  i2s_out_init(s_sample_rate_hz);

  tud_init(BOARD_TUD_RHPORT);
  board_init_after_tusb();

  memset(s_mute, 0, sizeof(s_mute));
  memset(s_volume_q8, 0, sizeof(s_volume_q8));

  while (true) {
    tud_task();
    audio_task();
  }
}
