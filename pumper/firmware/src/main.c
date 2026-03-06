#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "bsp/board_api.h"
#include "tusb.h"

#include "i2s_out.h"
#include "usb_descriptors.h"

#define AUDIO_SAMPLE_RATE_HZ 48000u
#define AUDIO_CHANNELS 2u
#define AUDIO_FRAME_BYTES 4u

static uint8_t s_mute[AUDIO_CHANNELS + 1];
static int16_t s_volume_q8[AUDIO_CHANNELS + 1];
static uint32_t s_sample_rate_hz = AUDIO_SAMPLE_RATE_HZ;

static void audio_task(void) {
  int16_t sample_buf[CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX / sizeof(int16_t)];

  while (tud_audio_available() >= AUDIO_FRAME_BYTES) {
    size_t free_frames = i2s_out_free_frames();
    if (free_frames == 0) {
      break;
    }

    uint16_t to_read = tud_audio_available();
    if (to_read > sizeof(sample_buf)) {
      to_read = sizeof(sample_buf);
    }

    to_read = (uint16_t) (to_read & ~(AUDIO_FRAME_BYTES - 1u));
    if (to_read == 0) {
      break;
    }

    uint16_t got = tud_audio_read(sample_buf, to_read);
    uint16_t frames = (uint16_t) (got / AUDIO_FRAME_BYTES);
    size_t accepted = i2s_out_write_stereo16(sample_buf, frames);

    if (accepted < frames) {
      break;
    }
  }
}

int main(void) {
  board_init();
  i2s_out_init(AUDIO_SAMPLE_RATE_HZ);

  tusb_rhport_init_t dev_init = {
      .role = TUSB_ROLE_DEVICE,
      .speed = TUSB_SPEED_AUTO,
  };
  tusb_init(BOARD_TUD_RHPORT, &dev_init);
  board_init_after_tusb();

  memset(s_mute, 0, sizeof(s_mute));
  memset(s_volume_q8, 0, sizeof(s_volume_q8));

  while (true) {
    tud_task();
    audio_task();
  }
}

bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
  (void) rhport;
  (void) p_request;
  return true;
}

bool tud_audio_set_itf_close_ep_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
  (void) rhport;
  (void) p_request;
  return true;
}

bool tud_audio_set_req_ep_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *pBuff) {
  (void) rhport;
  uint8_t ctrl_sel = TU_U16_HIGH(p_request->wValue);
  if (ctrl_sel == AUDIO10_EP_CTRL_SAMPLING_FREQ && p_request->bRequest == AUDIO10_CS_REQ_SET_CUR) {
    TU_VERIFY(p_request->wLength == 3);
    uint32_t req_rate = (uint32_t) pBuff[0] | ((uint32_t) pBuff[1] << 8u) | ((uint32_t) pBuff[2] << 16u);
    if (req_rate == AUDIO_SAMPLE_RATE_HZ) {
      s_sample_rate_hz = req_rate;
      i2s_out_set_sample_rate(req_rate);
      return true;
    }
  }
  return false;
}

bool tud_audio_get_req_ep_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
  uint8_t ctrl_sel = TU_U16_HIGH(p_request->wValue);
  if (ctrl_sel == AUDIO10_EP_CTRL_SAMPLING_FREQ && p_request->bRequest == AUDIO10_CS_REQ_GET_CUR) {
    uint8_t freq[3] = {
        (uint8_t) (s_sample_rate_hz & 0xffu),
        (uint8_t) ((s_sample_rate_hz >> 8u) & 0xffu),
        (uint8_t) ((s_sample_rate_hz >> 16u) & 0xffu),
    };
    return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, freq, sizeof(freq));
  }
  return false;
}

bool tud_audio_set_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *buf) {
  (void) rhport;
  uint8_t channel = TU_U16_LOW(p_request->wValue);
  uint8_t ctrl_sel = TU_U16_HIGH(p_request->wValue);
  uint8_t entity_id = TU_U16_HIGH(p_request->wIndex);

  if (entity_id != UAC1_ENTITY_FEATURE_UNIT) {
    return false;
  }
  if (channel > AUDIO_CHANNELS) {
    return false;
  }

  if (ctrl_sel == AUDIO10_FU_CTRL_MUTE && p_request->bRequest == AUDIO10_CS_REQ_SET_CUR) {
    TU_VERIFY(p_request->wLength == 1);
    s_mute[channel] = buf[0];
    return true;
  }

  if (ctrl_sel == AUDIO10_FU_CTRL_VOLUME && p_request->bRequest == AUDIO10_CS_REQ_SET_CUR) {
    TU_VERIFY(p_request->wLength == 2);
    s_volume_q8[channel] = (int16_t) tu_unaligned_read16(buf);
    return true;
  }

  return false;
}

bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
  uint8_t channel = TU_U16_LOW(p_request->wValue);
  uint8_t ctrl_sel = TU_U16_HIGH(p_request->wValue);
  uint8_t entity_id = TU_U16_HIGH(p_request->wIndex);

  if (entity_id != UAC1_ENTITY_FEATURE_UNIT || channel > AUDIO_CHANNELS) {
    return false;
  }

  if (ctrl_sel == AUDIO10_FU_CTRL_MUTE) {
    return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &s_mute[channel], 1);
  }

  if (ctrl_sel == AUDIO10_FU_CTRL_VOLUME) {
    if (p_request->bRequest == AUDIO10_CS_REQ_GET_CUR) {
      return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &s_volume_q8[channel], sizeof(s_volume_q8[channel]));
    }
    if (p_request->bRequest == AUDIO10_CS_REQ_GET_MIN) {
      int16_t min_q8 = (int16_t) (-90 * 256);
      return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &min_q8, sizeof(min_q8));
    }
    if (p_request->bRequest == AUDIO10_CS_REQ_GET_MAX) {
      int16_t max_q8 = (int16_t) (0 * 256);
      return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &max_q8, sizeof(max_q8));
    }
    if (p_request->bRequest == AUDIO10_CS_REQ_GET_RES) {
      int16_t res_q8 = (int16_t) (1 * 256);
      return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &res_q8, sizeof(res_q8));
    }
  }

  return false;
}

void tud_audio_feedback_params_cb(uint8_t func_id, uint8_t alt_itf, audio_feedback_params_t *feedback_param) {
  (void) func_id;
  (void) alt_itf;
  feedback_param->method = AUDIO_FEEDBACK_METHOD_FIFO_COUNT;
  feedback_param->sample_freq = s_sample_rate_hz;
}
