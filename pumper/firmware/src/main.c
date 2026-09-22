#include <stdbool.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "bsp/board_api.h"
#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/pwm.h"
#include "hardware/vreg.h"
#include "hardware/watchdog.h"
#include "pico/bootrom.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pico/util/queue.h"
#include "tusb.h"

#include "audio_feedback.h"
#include "audio_format.h"
#include "eq_config.h"
#include "eq_dsp.h"
#include "audio_controls.h"
#include "hid_activity.h"
#include "eq_protocol.h"
#include "eq_settings.h"
#include "i2s_out.h"
#include "quirk_os_guessing.h"
#include "usb_descriptors.h"

#define AUDIO_CHANNELS 2u
#define AUDIO_BLOCK_COUNT I2S_AUDIO_BLOCK_COUNT
#define METER_REPORT_INTERVAL_MIN_MS 20u
#define METER_REPORT_INTERVAL_MAX_MS 250u
#define METER_TIMEOUT_MIN_MS 250u
#define METER_TIMEOUT_MAX_MS 5000u
#define DEVICE_RESET_ACK_DELAY_US 250000u
#define DEVICE_RESET_FALLBACK_DELAY_US 1000000u
#define TEMPERATURE_SAMPLE_COUNT 8u
#define ADC_REFERENCE_VOLTAGE 3.3f
#define ADC_MAX_COUNTS 4096.0f
#define TEMPERATURE_REFERENCE_C 27.0f
#define TEMPERATURE_REFERENCE_VOLTAGE 0.706f
#define TEMPERATURE_VOLTAGE_SLOPE 0.001721f
#define IDLE_SYS_CLOCK_KHZ 150000u
#define STREAM_SYS_CLOCK_KHZ 180000u

// GPIO pins for the two status LEDs (active-low, driven via PWM).
#define LED_RED_PIN  10u  // Lit when USB audio streaming is active
#define LED_BLUE_PIN  9u  // Brightness tracks the final output peak level

// PWM configuration: 8-bit counter (wrap = 255).
// Maximum brightness is configured separately for each LED.
#define LED_PWM_WRAP             255u
#define LED_RED_MAX_BRIGHTNESS   ((LED_PWM_WRAP * 40u) / 100u)
#define LED_BLUE_MAX_BRIGHTNESS  ((LED_PWM_WRAP * 60u) / 100u)

static uint32_t const sample_rates[] = {44100u, 48000u, 88200u, 96000u, 176400u, 192000u};
#define N_SAMPLE_RATES TU_ARRAY_SIZE(sample_rates)

// Host controls only the stereo master (index 0). Channel slots stay zero
// for compatibility with the existing HID audio-controls response.
static int8_t s_mute[AUDIO_CHANNELS + 1];

// Volume in UAC2 Q8.8 fixed-point dB (0 = 0 dB, -256 = -1 dB).
// Range advertised: -50 dB (–12800) to 0 dB, in 1 dB (256) steps.
static int16_t s_volume_q8[AUDIO_CHANNELS + 1];

static volatile uint32_t s_sample_rate_hz = 48000u;

// Fixed pool passed from core 0 (USB) to core 1 (DSP) without allocating.
typedef i2s_audio_block_t audio_block_t;
static audio_block_t s_audio_blocks[AUDIO_BLOCK_COUNT];
static uint8_t s_usb_packet[CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX];
static queue_t s_free_audio_blocks;
static queue_t s_pending_audio_blocks;

static crossfeed_config_t s_crossfeed_live, s_crossfeed_saved;
static output_processing_config_t s_output_live, s_output_saved;
static hid_activity_t s_hid_activity;
static eq_config_t s_saved_config;
static volatile uint32_t s_config_generation = 1u;
static volatile uint32_t s_applied_generation = 0u;
static volatile uint32_t s_saved_generation = 0u;
static volatile uint32_t s_backpressure_events = 0u;
static volatile bool s_streaming_active = false;
static volatile uint32_t s_stream_generation = 0u;
static volatile audio_sample_format_t s_audio_format = AUDIO_FORMAT_PCM16;
static volatile bool s_dsp_core_ready = false;
static volatile uint32_t s_dsp_max_block_us = 0u;
static uint8_t s_active_profile = 0u;
static uint8_t s_persisted_profile = 0u;

typedef struct {
  eq_level_metrics_t pre_eq;
  eq_level_metrics_t post_eq;
  uint32_t frame_count;
  bool limiter_active;
} meter_accumulator_t;

typedef struct {
  volatile uint32_t sequence;
  eq_config_t config;
  uint32_t generation;
} config_snapshot_t;

typedef struct {
  volatile uint32_t sequence;
  crossfeed_config_t crossfeed;
  output_processing_config_t output;
  float gains[2];
  uint32_t generation;
} controls_snapshot_t;

static config_snapshot_t s_config_snapshots[2];
static controls_snapshot_t s_controls_snapshots[2];
static volatile uint32_t s_config_snapshot_index;
static volatile uint32_t s_controls_snapshot_index;
static volatile uint32_t s_audio_controls_generation = 1u;
static meter_accumulator_t s_meter_accumulators[2];
static volatile uint32_t s_meter_accumulator_index;
static volatile uint32_t s_meter_writers[2];
static bool s_meter_configured = false;
static volatile bool s_meter_active = false;
static uint16_t s_meter_report_interval_ms = 40u;
static uint16_t s_meter_timeout_ms = 1250u;
static uint64_t s_meter_deadline_us = 0u;
static uint64_t s_meter_next_report_us = 0u;
static uint32_t s_meter_sequence = 0u;
static audio_feedback_controller_t s_feedback_controller;
static bool s_feedback_initialized = false;
static uint64_t s_feedback_next_update_us = 0u;

static uint8_t s_hid_response[EQ_PROTOCOL_REPORT_SIZE];
static bool s_hid_response_pending = false;
typedef enum {
  DEVICE_RESET_NONE,
  DEVICE_RESET_RESTART,
  DEVICE_RESET_BOOTSEL,
} device_reset_action_t;
static device_reset_action_t s_device_reset_action = DEVICE_RESET_NONE;
static bool s_device_reset_response_in_flight = false;
static uint64_t s_device_reset_deadline_us = 0u;
typedef enum {
  FLASH_ACTION_SAVE_PROFILE,
  FLASH_ACTION_SET_DEFAULT_PROFILE,
  FLASH_ACTION_DELETE_PROFILE,
  FLASH_ACTION_SAVE_CROSSFEED,
  FLASH_ACTION_SAVE_OUTPUT_PROCESSING,
} flash_action_t;
static bool s_flash_write_pending = false;
static flash_action_t s_flash_action = FLASH_ACTION_SAVE_PROFILE;
static uint8_t s_flash_request_opcode = 0u;
static uint16_t s_flash_request_id = 0u;
static uint8_t s_flash_profile_index = 0u;

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

static void red_led_set(bool on) {
  uint slice = pwm_gpio_to_slice_num(LED_RED_PIN);
  uint channel = pwm_gpio_to_channel(LED_RED_PIN);
  pwm_set_chan_level(slice, channel, on ? LED_RED_MAX_BRIGHTNESS : 0);
}

static void led_set_level(uint pin, uint16_t level) {
  uint slice = pwm_gpio_to_slice_num(pin);
  uint channel = pwm_gpio_to_channel(pin);
  if (level > LED_PWM_WRAP) level = LED_PWM_WRAP;
  pwm_set_chan_level(slice, channel, level);
}

static void set_performance_clock(bool streaming) {
  if (streaming) {
    vreg_set_voltage(VREG_VOLTAGE_1_15);
    sleep_us(1000u);
    set_sys_clock_khz(STREAM_SYS_CLOCK_KHZ, true);
  } else {
    set_sys_clock_khz(IDLE_SYS_CLOCK_KHZ, true);
    vreg_set_voltage(VREG_VOLTAGE_1_10);
  }
  i2s_out_set_format(s_sample_rate_hz, s_audio_format);
}

static void set_streaming_state(bool streaming) {
  if (streaming == s_streaming_active) return;
  if (streaming) set_performance_clock(true);

  s_stream_generation++;
  s_streaming_active = streaming;
  s_feedback_initialized = false;
  s_feedback_next_update_us = 0u;
  s_dsp_max_block_us = 0u;
  i2s_out_set_streaming(streaming);
  if (!streaming) {
    audio_block_t *block;
    while (queue_try_remove(&s_pending_audio_blocks, &block))
      queue_add_blocking(&s_free_audio_blocks, &block);
    led_set_level(LED_BLUE_PIN, 0u);
    set_performance_clock(false);
  }
}

static int32_t chip_temperature_millicelsius(void) {
  uint32_t sample_total = 0u;
  for (uint32_t i = 0; i < TEMPERATURE_SAMPLE_COUNT; i++) sample_total += adc_read();
  float average_sample = (float)sample_total / (float)TEMPERATURE_SAMPLE_COUNT;
  float voltage = average_sample * (ADC_REFERENCE_VOLTAGE / ADC_MAX_COUNTS);
  float celsius = TEMPERATURE_REFERENCE_C -
                  (voltage - TEMPERATURE_REFERENCE_VOLTAGE) / TEMPERATURE_VOLTAGE_SLOPE;
  return (int32_t)lrintf(celsius * 1000.0f);
}

// Respond to an unhandled control request with a zero-filled buffer.
// Used as a safe fallback for GET requests on unknown entities.
static bool send_zero_control(uint8_t rhport, tusb_control_request_t const *p_request) {
  static uint8_t const zero_buf[64] = {0};
  uint16_t len = tu_le16toh(p_request->wLength);
  if (len > sizeof(zero_buf)) len = sizeof(zero_buf);
  return tud_control_xfer(rhport, p_request, (void *) zero_buf, len);
}

static bool sample_rate_supported(uint32_t sample_rate_hz) {
  for (uint8_t i = 0; i < N_SAMPLE_RATES; i++) {
    if (sample_rates[i] == sample_rate_hz) return true;
  }
  return false;
}

static bool handle_sample_rate_change(uint32_t new_rate_hz) {
  if (!sample_rate_supported(new_rate_hz) || !audio_format_rate_valid(s_audio_format, new_rate_hz)) return false;
  bool resume = s_streaming_active;
  if (resume) set_streaming_state(false);
  if (resume) tud_audio_clear_ep_out_ff();
  s_sample_rate_hz = new_rate_hz;
  s_feedback_initialized = false;
  i2s_out_set_format(s_sample_rate_hz, s_audio_format);
  if (resume) set_streaming_state(true);
  return true;
}

static eq_config_t config_snapshot(uint32_t *generation) {
  for (;;) {
    uint32_t index = __atomic_load_n(&s_config_snapshot_index, __ATOMIC_ACQUIRE);
    config_snapshot_t const *snapshot = &s_config_snapshots[index];
    uint32_t before = __atomic_load_n(&snapshot->sequence, __ATOMIC_ACQUIRE);
    if (before & 1u) continue;
    eq_config_t config = snapshot->config;
    uint32_t value = snapshot->generation;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if (before == __atomic_load_n(&snapshot->sequence, __ATOMIC_RELAXED) &&
        index == __atomic_load_n(&s_config_snapshot_index, __ATOMIC_RELAXED)) {
      if (generation) *generation = value;
      return config;
    }
  }
}

static void publish_config(eq_config_t const *config) {
  uint32_t index = __atomic_load_n(&s_config_snapshot_index, __ATOMIC_RELAXED) ^ 1u;
  config_snapshot_t *snapshot = &s_config_snapshots[index];
  __atomic_add_fetch(&snapshot->sequence, 1u, __ATOMIC_RELAXED);
  snapshot->config = *config;
  snapshot->generation = ++s_config_generation;
  __atomic_add_fetch(&snapshot->sequence, 1u, __ATOMIC_RELEASE);
  __atomic_store_n(&s_config_snapshot_index, index, __ATOMIC_RELEASE);
}

static controls_snapshot_t controls_snapshot(void) {
  for (;;) {
    uint32_t index = __atomic_load_n(&s_controls_snapshot_index, __ATOMIC_ACQUIRE);
    controls_snapshot_t const *snapshot = &s_controls_snapshots[index];
    uint32_t before = __atomic_load_n(&snapshot->sequence, __ATOMIC_ACQUIRE);
    if (before & 1u) continue;
    controls_snapshot_t copy = *snapshot;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if (before == __atomic_load_n(&snapshot->sequence, __ATOMIC_RELAXED) &&
        index == __atomic_load_n(&s_controls_snapshot_index, __ATOMIC_RELAXED)) return copy;
  }
}

static void publish_audio_controls(void) {
  uint32_t index = __atomic_load_n(&s_controls_snapshot_index, __ATOMIC_RELAXED) ^ 1u;
  controls_snapshot_t *snapshot = &s_controls_snapshots[index];
  __atomic_add_fetch(&snapshot->sequence, 1u, __ATOMIC_RELAXED);
  snapshot->crossfeed = s_crossfeed_live;
  snapshot->output = s_output_live;
  audio_effective_gains(s_volume_q8, s_mute, snapshot->gains);
  snapshot->generation = ++s_audio_controls_generation;
  __atomic_add_fetch(&snapshot->sequence, 1u, __ATOMIC_RELEASE);
  __atomic_store_n(&s_controls_snapshot_index, index, __ATOMIC_RELEASE);
}
static void apply_audio_controls(uint32_t *generation) {
  controls_snapshot_t snapshot = controls_snapshot();
  *generation = snapshot.generation;
  audio_controls_set_crossfeed(&snapshot.crossfeed);
  audio_controls_set_output_processing(&snapshot.output);
  audio_controls_set_gains(snapshot.gains);
}

static bool config_is_dirty(void) {
  eq_config_t config = config_snapshot(NULL);
  return !eq_config_equal(&config, &s_saved_config);
}

static void meter_reset(void) {
  uint32_t old = __atomic_load_n(&s_meter_accumulator_index, __ATOMIC_RELAXED);
  uint32_t next = old ^ 1u;
  memset(&s_meter_accumulators[next], 0, sizeof(s_meter_accumulators[next]));
  __atomic_store_n(&s_meter_accumulator_index, next, __ATOMIC_RELEASE);
  while (__atomic_load_n(&s_meter_writers[old], __ATOMIC_ACQUIRE)) tight_loop_contents();
  memset(&s_meter_accumulators[old], 0, sizeof(s_meter_accumulators[old]));
}

static void meter_merge_level(eq_level_metrics_t *accumulator,
                              eq_level_metrics_t const *block) {
  if (block->left_peak > accumulator->left_peak) accumulator->left_peak = block->left_peak;
  if (block->right_peak > accumulator->right_peak) accumulator->right_peak = block->right_peak;
  accumulator->left_square_sum += block->left_square_sum;
  accumulator->right_square_sum += block->right_square_sum;
}

static void meter_accumulate(eq_level_metrics_t const *pre_eq,
                             eq_level_metrics_t const *post_eq, uint16_t frames,
                             bool limiter_active) {
  if (!s_meter_active) return;
  for (;;) {
    uint32_t index = __atomic_load_n(&s_meter_accumulator_index, __ATOMIC_ACQUIRE);
    __atomic_add_fetch(&s_meter_writers[index], 1u, __ATOMIC_ACQUIRE);
    if (index == __atomic_load_n(&s_meter_accumulator_index, __ATOMIC_RELAXED)) {
      meter_merge_level(&s_meter_accumulators[index].pre_eq, pre_eq);
      meter_merge_level(&s_meter_accumulators[index].post_eq, post_eq);
      s_meter_accumulators[index].frame_count += frames;
      s_meter_accumulators[index].limiter_active |= limiter_active;
      __atomic_sub_fetch(&s_meter_writers[index], 1u, __ATOMIC_RELEASE);
      return;
    }
    __atomic_sub_fetch(&s_meter_writers[index], 1u, __ATOMIC_RELEASE);
  }
}

static meter_accumulator_t meter_take(void) {
  uint32_t old = __atomic_load_n(&s_meter_accumulator_index, __ATOMIC_RELAXED);
  uint32_t next = old ^ 1u;
  memset(&s_meter_accumulators[next], 0, sizeof(s_meter_accumulators[next]));
  __atomic_store_n(&s_meter_accumulator_index, next, __ATOMIC_RELEASE);
  while (__atomic_load_n(&s_meter_writers[old], __ATOMIC_ACQUIRE)) tight_loop_contents();
  meter_accumulator_t result = s_meter_accumulators[old];
  memset(&s_meter_accumulators[old], 0, sizeof(s_meter_accumulators[old]));
  return result;
}

static void meter_keepalive(void) {
  uint64_t now = time_us_64();
  s_meter_active = true;
  s_meter_deadline_us = now + (uint64_t)s_meter_timeout_ms * 1000u;
  if (s_meter_next_report_us == 0u) s_meter_next_report_us = now;
}

static void dsp_core_main(void) {
  eq_settings_core_init();
  uint32_t local_generation;
  eq_config_t local_config = config_snapshot(&local_generation);
  controls_snapshot_t initial_controls = controls_snapshot();
  uint32_t local_sample_rate = s_sample_rate_hz;
  eq_init(local_sample_rate, &local_config);
  audio_controls_init(local_sample_rate, &initial_controls.crossfeed, &initial_controls.output);
  uint32_t local_controls_generation = 0u;
  uint32_t local_stream_generation = s_stream_generation;
  apply_audio_controls(&local_controls_generation);
  s_applied_generation = local_generation;
  s_dsp_core_ready = true;

  while (true) {
    audio_block_t *block;
    queue_remove_blocking(&s_pending_audio_blocks, &block);

    if (local_sample_rate != s_sample_rate_hz) {
      local_sample_rate = s_sample_rate_hz;
      eq_set_sample_rate(local_sample_rate);
      audio_controls_reset(local_sample_rate);
    }
    if (s_audio_controls_generation != local_controls_generation)
      apply_audio_controls(&local_controls_generation);
    if (local_stream_generation != block->stream_generation) {
      local_stream_generation = block->stream_generation;
      eq_reset_state();
      audio_controls_reset(local_sample_rate);
    }
    if (s_config_generation != local_generation) {
      uint32_t desired_generation;
      eq_config_t desired = config_snapshot(&desired_generation);
      if (eq_set_config(&desired)) {
        local_generation = desired_generation;
        s_applied_generation = desired_generation;
      }
    }

    if (s_streaming_active && block->stream_generation == s_stream_generation) {
      bool const measure_block = s_meter_active;
      eq_block_metrics_t metrics;
      uint32_t started_us = time_us_32();
      eq_process_interleaved_stereo(block->data.samples, block->frames, &metrics, measure_block);
      uint32_t elapsed_us = time_us_32() - started_us;
      if (elapsed_us > s_dsp_max_block_us) s_dsp_max_block_us = elapsed_us;

      // Reuse the saturated output metrics collected for HID; no second sample scan.
      uint32_t peak = metrics.post_eq.left_peak;
      if (metrics.post_eq.right_peak > peak) peak = metrics.post_eq.right_peak;
      led_set_level(LED_BLUE_PIN, (uint16_t)((peak * LED_BLUE_MAX_BRIGHTNESS) / 32768u));
      if (measure_block) {
        meter_accumulate(&metrics.pre_eq, &metrics.post_eq, block->frames,
                         metrics.limiter_active);
      }

      block->word_count = (uint16_t)audio_format_pack_i2s(
          block->data.words, block->data.samples, block->frames, block->format);
      if (i2s_out_submit(block)) block = NULL;
    }
    if (block) queue_add_blocking(&s_free_audio_blocks, &block);
  }
}

// Backpressure is retained in TinyUSB's software buffer when all audio blocks
// are in flight.
static void audio_task(void) {
  static bool backpressure_latched = false;
  while (tud_audio_available() >= audio_format_frame_bytes(s_audio_format)) {
    audio_block_t *block;
    if (!queue_try_remove(&s_free_audio_blocks, &block)) {
      if (!backpressure_latched) {
        s_backpressure_events++;
        backpressure_latched = true;
      }
      break;
    }
    backpressure_latched = false;

    size_t frame_bytes = audio_format_frame_bytes(s_audio_format);
    uint16_t to_read = tud_audio_available();
    if (to_read > sizeof(s_usb_packet)) to_read = sizeof(s_usb_packet);
    to_read = (uint16_t)(to_read - to_read % frame_bytes);
    if (to_read == 0) {
      queue_add_blocking(&s_free_audio_blocks, &block);
      break;
    }

    uint16_t got = tud_audio_read(s_usb_packet, to_read);
    uint16_t frames = (uint16_t)audio_format_decode(
        block->data.samples, I2S_AUDIO_BLOCK_MAX_FRAMES, s_usb_packet, got, s_audio_format);

    if (frames == 0u || !s_streaming_active) {
      queue_add_blocking(&s_free_audio_blocks, &block);
      continue;
    }
    block->frames = frames;
    block->stream_generation = s_stream_generation;
    block->format = s_audio_format;
    queue_add_blocking(&s_pending_audio_blocks, &block);
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

static bool tud_audio_feature_unit_get_request(uint8_t rhport, audio_control_request_t const *request) {
  TU_VERIFY(request->bChannelNumber == 0u);
  uint16_t length = tu_le16toh(request->wLength);
  if (request->bControlSelector == AUDIO_FU_CTRL_MUTE && request->bRequest == AUDIO_CS_REQ_CUR) {
    TU_VERIFY(length == 1u);
    audio_control_cur_1_t mute1 = {.bCur = (uint8_t) s_mute[request->bChannelNumber]};
    return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *) request, &mute1, sizeof(mute1));
  }

  if (request->bControlSelector == AUDIO_FU_CTRL_VOLUME) {
    if (request->bRequest == AUDIO_CS_REQ_RANGE) {
      TU_VERIFY(length == 2u || length == 8u);
      audio_control_range_2_n_t(1) range_vol = {
          .wNumSubRanges = tu_htole16(1),
          .subrange[0] = { .bMin = tu_htole16(-12800), .bMax = tu_htole16(0), .bRes = tu_htole16(256) },
      };
      return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *) request, &range_vol, sizeof(range_vol));
    } else if (request->bRequest == AUDIO_CS_REQ_CUR) {
      TU_VERIFY(length == 2u);
      audio_control_cur_2_t cur_vol = {.bCur = tu_htole16(s_volume_q8[request->bChannelNumber])};
      return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *) request, &cur_vol, sizeof(cur_vol));
    }
  }
  return false;
}

static bool tud_audio_feature_unit_set_request(audio_control_request_t const *request, uint8_t const *buf) {
  TU_VERIFY(request->bRequest == AUDIO_CS_REQ_CUR);
  TU_VERIFY(audio_host_control_set(request->bChannelNumber, request->bControlSelector, buf,
                                   tu_le16toh(request->wLength), s_volume_q8, s_mute));
  publish_audio_controls();
  return true;
}

bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
  audio_control_request_t const *request = (audio_control_request_t const *) p_request;
  if (request->bEntityID == UAC2_ENTITY_CLOCK) return tud_audio_clock_get_request(rhport, request);
  if (request->bEntityID == UAC2_ENTITY_FEATURE_UNIT) return tud_audio_feature_unit_get_request(rhport, request);
  return send_zero_control(rhport, p_request);
}

// Both 3-byte (legacy UAC1-style) and 4-byte (UAC2) sample-rate encodings are accepted.
bool tud_audio_set_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *buf) {
  (void) rhport;
  audio_control_request_t const *request = (audio_control_request_t const *) p_request;

  if (request->bEntityID == UAC2_ENTITY_CLOCK && request->bControlSelector == AUDIO_CS_CTRL_SAM_FREQ &&
      request->bRequest == AUDIO_CS_REQ_CUR) {
    uint16_t const w_length = tu_le16toh(p_request->wLength);
    if (w_length == 3) {
      uint32_t rate = (uint32_t) buf[0] | ((uint32_t) buf[1] << 8u) | ((uint32_t) buf[2] << 16u);
      return handle_sample_rate_change(rate);
    }
    if (w_length == sizeof(audio_control_cur_4_t)) {
      return handle_sample_rate_change((uint32_t) ((audio_control_cur_4_t const *) buf)->bCur);
    }
  }

  if (request->bEntityID == UAC2_ENTITY_FEATURE_UNIT) return tud_audio_feature_unit_set_request(request, buf);
  return true;
}

bool tud_audio_set_itf_close_EP_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
  (void) rhport;
  uint8_t const itf = tu_u16_low(tu_le16toh(p_request->wIndex));
  uint8_t const alt = tu_u16_low(tu_le16toh(p_request->wValue));
  if (itf == ITF_NUM_AUDIO_STREAMING && alt == 0) {
    set_streaming_state(false);
  }
  return true;
}

// Alternate 0 is idle, 1 is 16-bit PCM, and 2 is packed 24-bit PCM.
bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
  (void) rhport;
  uint8_t const itf = tu_u16_low(tu_le16toh(p_request->wIndex));
  uint8_t const alt = tu_u16_low(tu_le16toh(p_request->wValue));
  if (itf == ITF_NUM_AUDIO_STREAMING) {
    if (alt > 2u) return false;
    if (alt == 0u) {
      set_streaming_state(false);
      return true;
    }
    audio_sample_format_t format = alt == 2u ? AUDIO_FORMAT_PCM24 : AUDIO_FORMAT_PCM16;
    if (!audio_format_rate_valid(format, s_sample_rate_hz)) return false;
    if (format != s_audio_format) {
      set_streaming_state(false);
      tud_audio_clear_ep_out_ff();
      s_audio_format = format;
      s_stream_generation++;
      i2s_out_set_format(s_sample_rate_hz, format);
    }
    set_streaming_state(true);
  }
  return true;
}

bool tud_audio_rx_done_pre_read_cb(uint8_t rhport, uint16_t n_bytes_received, uint8_t func_id, uint8_t ep_out, uint8_t cur_alt_setting) {
  (void) rhport;
  (void) func_id;
  (void) ep_out;
  (void) cur_alt_setting;
  (void) n_bytes_received;
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
      return handle_sample_rate_change(rate);
    }
    if (p_request->wLength == 4) {
      return handle_sample_rate_change((uint32_t) ((audio_control_cur_4_t const *) pBuff)->bCur);
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

static void hid_response_prepare(uint8_t opcode, uint16_t request_id, eq_protocol_status_t status,
                                 uint8_t payload_length) {
  eq_protocol_response_init(s_hid_response, opcode, request_id, status, payload_length);
  s_hid_response_pending = true;
}

static void hid_response_status(uint8_t opcode, uint16_t request_id) {
  eq_config_t config = config_snapshot(NULL);
  hid_response_prepare(opcode, request_id, EQ_STATUS_OK, EQ_PROTOCOL_STATUS_PAYLOAD_SIZE);
  uint8_t *payload = &s_hid_response[EQ_PROTOCOL_HEADER_SIZE];
  payload[0] = 3u;
  payload[1] = 3u;
  payload[2] = EQ_NUM_FILTERS;
  payload[3] = (s_streaming_active ? 0x01u : 0u) | (config_is_dirty() ? 0x02u : 0u) |
               (config.enabled ? 0x04u : 0u);
  eq_protocol_write_u32(payload + 4u, s_sample_rate_hz);
  eq_protocol_write_u32(payload + 8u, s_config_generation);
  eq_protocol_write_u32(payload + 12u, s_saved_generation);
  eq_protocol_write_u32(payload + 16u, s_applied_generation);
  eq_protocol_write_u32(payload + 20u, i2s_out_underrun_frames());
  eq_protocol_write_u32(payload + 24u, s_backpressure_events);
  eq_protocol_write_i32(payload + 28u, chip_temperature_millicelsius());
  eq_protocol_write_u32(payload + 32u, clock_get_hz(clk_sys));
  eq_protocol_write_u32(payload + 36u, s_dsp_max_block_us);
  eq_protocol_write_u32(payload + 40u, i2s_out_low_water_frames());
  payload[44] = s_audio_format == AUDIO_FORMAT_PCM24 ? 24u : 16u;
}

static void hid_response_crossfeed(uint8_t opcode, uint16_t request_id) {
  hid_response_prepare(opcode, request_id, EQ_STATUS_OK, CROSSFEED_STATE_SIZE);
  uint8_t *p = s_hid_response + EQ_PROTOCOL_HEADER_SIZE;
  crossfeed_state_encode(p, &s_crossfeed_live, &s_crossfeed_saved);
}

static void hid_response_output_processing(uint8_t opcode, uint16_t request_id) {
  hid_response_prepare(opcode, request_id, EQ_STATUS_OK, OUTPUT_PROCESSING_STATE_SIZE);
  output_processing_state_encode(s_hid_response + EQ_PROTOCOL_HEADER_SIZE,
                                 &s_output_live, &s_output_saved);
}

static void hid_process_command(eq_protocol_packet_t const *packet) {
  if ((packet->opcode & EQ_OPCODE_RESPONSE) != 0u || packet->status != 0u) {
    hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_INVALID_PACKET, 0u);
    return;
  }

  switch (packet->opcode) {
    case EQ_OPCODE_HELLO:
    case EQ_OPCODE_GET_STATUS:
      if (packet->payload_length != 0u) {
        hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_INVALID_LENGTH, 0u);
      } else {
        hid_response_status(packet->opcode, packet->request_id);
      }
      break;

    case EQ_OPCODE_GET_AUDIO_CONTROLS:
      if (packet->payload_length != 0u) {
        hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_INVALID_LENGTH, 0u);
      } else {
        hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_OK,
                             EQ_PROTOCOL_AUDIO_CONTROLS_PAYLOAD_SIZE);
        uint8_t *p = s_hid_response + EQ_PROTOCOL_HEADER_SIZE;
        audio_host_controls_encode(p, s_volume_q8, s_mute);
      }
      break;
    case EQ_OPCODE_GET_CROSSFEED:
      if (packet->payload_length)
        hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_INVALID_LENGTH, 0u);
      else
        hid_response_crossfeed(packet->opcode, packet->request_id);
      break;
    case EQ_OPCODE_GET_OUTPUT_PROCESSING:
      if (packet->payload_length)
        hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_INVALID_LENGTH, 0u);
      else
        hid_response_output_processing(packet->opcode, packet->request_id);
      break;
    case EQ_OPCODE_SET_CROSSFEED: {
      crossfeed_config_t config;
      if (packet->payload_length != CROSSFEED_RECORD_SIZE) {
        hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_INVALID_LENGTH, 0u);
      } else if (!crossfeed_decode(packet->payload, packet->payload_length, &config)) {
        hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_OUT_OF_RANGE, 0u);
      } else {
        s_crossfeed_live = config;
        publish_audio_controls();
        hid_response_crossfeed(packet->opcode, packet->request_id);
      }
      break;
    }
    case EQ_OPCODE_SAVE_CROSSFEED:
      if (packet->payload_length)
        hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_INVALID_LENGTH, 0u);
      else if (s_flash_write_pending)
        hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_BUSY, 0u);
      else {
        s_flash_action = FLASH_ACTION_SAVE_CROSSFEED;
        s_flash_request_opcode = packet->opcode;
        s_flash_request_id = packet->request_id;
        s_flash_write_pending = true;
      }
      break;
    case EQ_OPCODE_SET_OUTPUT_PROCESSING: {
      output_processing_config_t config;
      if (packet->payload_length != OUTPUT_PROCESSING_RECORD_SIZE) {
        hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_INVALID_LENGTH, 0u);
      } else if (!output_processing_decode(packet->payload, packet->payload_length, &config)) {
        hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_OUT_OF_RANGE, 0u);
      } else {
        s_output_live = config;
        publish_audio_controls();
        hid_response_output_processing(packet->opcode, packet->request_id);
      }
      break;
    }
    case EQ_OPCODE_SAVE_OUTPUT_PROCESSING:
      if (packet->payload_length)
        hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_INVALID_LENGTH, 0u);
      else if (s_flash_write_pending)
        hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_BUSY, 0u);
      else {
        s_flash_action = FLASH_ACTION_SAVE_OUTPUT_PROCESSING;
        s_flash_request_opcode = packet->opcode;
        s_flash_request_id = packet->request_id;
        s_flash_write_pending = true;
      }
      break;

    case EQ_OPCODE_GET_GLOBAL: {
      if (packet->payload_length != 0u) {
        hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_INVALID_LENGTH, 0u);
        break;
      }
      eq_config_t config = config_snapshot(NULL);
      hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_OK, EQ_PROTOCOL_GLOBAL_PAYLOAD_SIZE);
      eq_protocol_encode_global(&s_hid_response[EQ_PROTOCOL_HEADER_SIZE], &config);
      break;
    }

    case EQ_OPCODE_GET_BAND: {
      if (packet->payload_length != 1u) {
        hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_INVALID_LENGTH, 0u);
        break;
      }
      uint8_t index = packet->payload[0];
      if (index >= EQ_NUM_FILTERS) {
        hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_INVALID_INDEX, 0u);
        break;
      }
      eq_config_t config = config_snapshot(NULL);
      hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_OK, EQ_PROTOCOL_BAND_PAYLOAD_SIZE);
      eq_protocol_encode_band(&s_hid_response[EQ_PROTOCOL_HEADER_SIZE], index, &config.filters[index]);
      break;
    }

    case EQ_OPCODE_GET_PROFILES: {
      if (packet->payload_length != 0u) {
        hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_INVALID_LENGTH, 0u);
        break;
      }
      eq_profile_state_t state;
      eq_settings_get_profile_state(&state);
      hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_OK,
                           EQ_PROTOCOL_PROFILE_STATE_PAYLOAD_SIZE);
      uint8_t *payload = &s_hid_response[EQ_PROTOCOL_HEADER_SIZE];
      payload[0] = EQ_PROFILE_COUNT;
      payload[1] = s_active_profile;
      payload[2] = s_persisted_profile;
      eq_protocol_write_u16(payload + 4u, state.present_mask);
      eq_protocol_write_u32(payload + 8u, state.bank_generation);
      break;
    }

    case EQ_OPCODE_SET_GLOBAL: {
      if (packet->payload_length != EQ_PROTOCOL_GLOBAL_PAYLOAD_SIZE) {
        hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_INVALID_LENGTH, 0u);
        break;
      }
      eq_config_t config = config_snapshot(NULL);
      if (!eq_protocol_decode_global(packet->payload, packet->payload_length, &config)) {
        hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_OUT_OF_RANGE, 0u);
        break;
      }
      publish_config(&config);
      hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_OK, EQ_PROTOCOL_GLOBAL_PAYLOAD_SIZE);
      eq_protocol_encode_global(&s_hid_response[EQ_PROTOCOL_HEADER_SIZE], &config);
      break;
    }

    case EQ_OPCODE_SET_BAND: {
      if (packet->payload_length != EQ_PROTOCOL_BAND_PAYLOAD_SIZE) {
        hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_INVALID_LENGTH, 0u);
        break;
      }
      uint8_t index;
      eq_filter_config_t filter;
      if (!eq_protocol_decode_band(packet->payload, packet->payload_length, &index, &filter)) {
        hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_OUT_OF_RANGE, 0u);
        break;
      }
      if (index >= EQ_NUM_FILTERS) {
        hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_INVALID_INDEX, 0u);
        break;
      }
      eq_config_t config = config_snapshot(NULL);
      config.filters[index] = filter;
      if (!eq_config_validate(&config)) {
        hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_OUT_OF_RANGE, 0u);
        break;
      }
      publish_config(&config);
      hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_OK, EQ_PROTOCOL_BAND_PAYLOAD_SIZE);
      eq_protocol_encode_band(&s_hid_response[EQ_PROTOCOL_HEADER_SIZE], index, &filter);
      break;
    }

    case EQ_OPCODE_RESTORE_DEFAULTS:
      if (packet->payload_length != 0u) {
        hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_INVALID_LENGTH, 0u);
      } else {
        eq_config_t defaults = k_eq_default_config;
        publish_config(&defaults);
        hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_OK, 0u);
      }
      break;

    case EQ_OPCODE_LOAD_PROFILE: {
      if (packet->payload_length != 1u) {
        hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_INVALID_LENGTH, 0u);
        break;
      }
      uint8_t index = packet->payload[0];
      eq_config_t config;
      uint32_t stored_generation;
      if (!eq_settings_load_profile(index, &config, &stored_generation)) {
        hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_INVALID_INDEX, 0u);
        break;
      }
      (void)stored_generation;
      publish_config(&config);
      s_saved_config = config;
      s_saved_generation = s_config_generation;
      s_active_profile = index;
      hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_OK,
                           EQ_PROTOCOL_PROFILE_RESULT_PAYLOAD_SIZE);
      uint8_t *payload = &s_hid_response[EQ_PROTOCOL_HEADER_SIZE];
      payload[0] = index;
      eq_protocol_write_u32(payload + 4u, s_config_generation);
      break;
    }

    case EQ_OPCODE_WRITE_FLASH:
      if (packet->payload_length != 0u) {
        hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_INVALID_LENGTH, 0u);
      } else if (s_flash_write_pending) {
        hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_BUSY, 0u);
      } else {
        s_flash_write_pending = true;
        s_flash_request_opcode = packet->opcode;
        s_flash_request_id = packet->request_id;
        s_flash_profile_index = s_active_profile;
        s_flash_action = FLASH_ACTION_SAVE_PROFILE;
      }
      break;

    case EQ_OPCODE_SAVE_PROFILE:
      if (packet->payload_length != 1u) {
        hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_INVALID_LENGTH, 0u);
      } else if (packet->payload[0] >= EQ_PROFILE_COUNT) {
        hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_INVALID_INDEX, 0u);
      } else if (s_flash_write_pending) {
        hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_BUSY, 0u);
      } else {
        s_flash_write_pending = true;
        s_flash_request_opcode = packet->opcode;
        s_flash_request_id = packet->request_id;
        s_flash_profile_index = packet->payload[0];
        s_flash_action = FLASH_ACTION_SAVE_PROFILE;
      }
      break;

    case EQ_OPCODE_SET_DEFAULT_PROFILE: {
      if (packet->payload_length != 1u) {
        hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_INVALID_LENGTH, 0u);
        break;
      }
      uint8_t index = packet->payload[0];
      eq_profile_state_t state;
      eq_settings_get_profile_state(&state);
      if (index >= EQ_PROFILE_COUNT || (state.present_mask & (1u << index)) == 0u) {
        hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_INVALID_INDEX, 0u);
      } else if (s_flash_write_pending) {
        hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_BUSY, 0u);
      } else {
        s_flash_write_pending = true;
        s_flash_request_opcode = packet->opcode;
        s_flash_request_id = packet->request_id;
        s_flash_profile_index = index;
        s_flash_action = FLASH_ACTION_SET_DEFAULT_PROFILE;
      }
      break;
    }

    case EQ_OPCODE_DELETE_PROFILE: {
      if (packet->payload_length != 1u) {
        hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_INVALID_LENGTH, 0u);
        break;
      }
      uint8_t index = packet->payload[0];
      eq_profile_state_t state;
      eq_settings_get_profile_state(&state);
      if (index >= EQ_PROFILE_COUNT || (state.present_mask & (1u << index)) == 0u) {
        hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_INVALID_INDEX, 0u);
      } else if (s_flash_write_pending) {
        hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_BUSY, 0u);
      } else {
        s_flash_write_pending = true;
        s_flash_request_opcode = packet->opcode;
        s_flash_request_id = packet->request_id;
        s_flash_profile_index = index;
        s_flash_action = FLASH_ACTION_DELETE_PROFILE;
      }
      break;
    }

    case EQ_OPCODE_METER_START: {
      if (packet->payload_length != EQ_PROTOCOL_METER_CONFIG_PAYLOAD_SIZE) {
        hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_INVALID_LENGTH, 0u);
        break;
      }
      uint16_t report_interval_ms = eq_protocol_read_u16(packet->payload);
      uint16_t timeout_ms = eq_protocol_read_u16(packet->payload + 2u);
      if (report_interval_ms < METER_REPORT_INTERVAL_MIN_MS ||
          report_interval_ms > METER_REPORT_INTERVAL_MAX_MS || timeout_ms < METER_TIMEOUT_MIN_MS ||
          timeout_ms > METER_TIMEOUT_MAX_MS || timeout_ms <= report_interval_ms) {
        hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_OUT_OF_RANGE, 0u);
        break;
      }
      s_meter_report_interval_ms = report_interval_ms;
      s_meter_timeout_ms = timeout_ms;
      s_meter_configured = true;
      s_meter_sequence = 0u;
      s_meter_next_report_us = 0u;
      meter_reset();
      meter_keepalive();
      hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_OK,
                           EQ_PROTOCOL_METER_CONFIG_PAYLOAD_SIZE);
      eq_protocol_write_u16(&s_hid_response[EQ_PROTOCOL_HEADER_SIZE], s_meter_report_interval_ms);
      eq_protocol_write_u16(&s_hid_response[EQ_PROTOCOL_HEADER_SIZE + 2u], s_meter_timeout_ms);
      break;
    }

    case EQ_OPCODE_METER_KEEPALIVE:
      if (packet->payload_length != 0u) {
        hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_INVALID_LENGTH, 0u);
      } else if (!s_meter_configured) {
        hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_INVALID_PACKET, 0u);
      } else {
        meter_keepalive();
        hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_OK, 0u);
      }
      break;

    case EQ_OPCODE_METER_STOP:
      if (packet->payload_length != 0u) {
        hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_INVALID_LENGTH, 0u);
      } else {
        s_meter_active = false;
        s_meter_configured = false;
        s_meter_next_report_us = 0u;
        meter_reset();
        hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_OK, 0u);
      }
      break;

    case EQ_OPCODE_RESTART_DEVICE:
    case EQ_OPCODE_ENTER_BOOTSEL:
      if (packet->payload_length != 0u) {
        hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_INVALID_LENGTH, 0u);
      } else {
        s_meter_active = false;
        s_meter_configured = false;
        s_meter_next_report_us = 0u;
        meter_reset();
        s_device_reset_action = packet->opcode == EQ_OPCODE_ENTER_BOOTSEL
                                    ? DEVICE_RESET_BOOTSEL
                                    : DEVICE_RESET_RESTART;
        hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_OK, 0u);
      }
      break;

    default:
      hid_response_prepare(packet->opcode, packet->request_id, EQ_STATUS_INVALID_COMMAND, 0u);
      break;
  }
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen) {
  (void)instance;
  (void)report_id;
  (void)report_type;
  (void)buffer;
  (void)reqlen;
  return 0u;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize) {
  (void)instance;
  (void)report_id;
  if (report_type == HID_REPORT_TYPE_OUTPUT)
    hid_activity_notify(&s_hid_activity, time_us_32());
  if (report_type != HID_REPORT_TYPE_OUTPUT || s_hid_response_pending || s_flash_write_pending ||
      s_device_reset_action != DEVICE_RESET_NONE) return;
  eq_protocol_packet_t packet;
  if (eq_protocol_decode(buffer, bufsize, &packet)) hid_process_command(&packet);
}

static void hid_control_task(void) {
  if (s_flash_write_pending && !s_hid_response_pending) {
    bool const resume_stream = s_streaming_active;
    if (resume_stream) {
      s_stream_generation++;
      s_streaming_active = false;
      s_feedback_initialized = false;
    }
    i2s_out_enter_flash_mute();
    audio_block_t *block;
    while (queue_try_remove(&s_pending_audio_blocks, &block))
      queue_add_blocking(&s_free_audio_blocks, &block);
    led_set_level(LED_BLUE_PIN, 0u);

    uint32_t generation;
    eq_config_t config = config_snapshot(&generation);
    bool succeeded;
    if (s_flash_action == FLASH_ACTION_SAVE_PROFILE) {
      succeeded = eq_settings_save_profile(s_flash_profile_index, &config, generation);
    } else if (s_flash_action == FLASH_ACTION_SET_DEFAULT_PROFILE) {
      succeeded = eq_settings_set_default_profile(s_flash_profile_index);
    } else if (s_flash_action == FLASH_ACTION_SAVE_CROSSFEED) {
      succeeded = eq_settings_save_crossfeed(&s_crossfeed_live);
      if (succeeded)
        eq_settings_get_crossfeed(&s_crossfeed_saved);
    } else if (s_flash_action == FLASH_ACTION_SAVE_OUTPUT_PROCESSING) {
      succeeded = eq_settings_save_output_processing(&s_output_live);
      if (succeeded) eq_settings_get_output_processing(&s_output_saved);
    } else {
      succeeded = eq_settings_delete_profile(s_flash_profile_index);
    }
    if (succeeded) {
      eq_profile_state_t state;
      eq_settings_get_profile_state(&state);
      s_persisted_profile = state.default_profile;
      if (s_flash_action == FLASH_ACTION_SAVE_PROFILE) {
        s_saved_config = config;
        s_saved_generation = generation;
        s_active_profile = s_flash_profile_index;
      }
    }

    if (resume_stream) {
      tud_audio_clear_ep_out_ff();
      s_stream_generation++;
      s_streaming_active = true;
      s_feedback_next_update_us = 0u;
    }
    i2s_out_exit_flash_mute(resume_stream);

    hid_response_prepare(s_flash_request_opcode, s_flash_request_id,
                         succeeded ? EQ_STATUS_OK : EQ_STATUS_STORAGE_ERROR,
                         succeeded ? EQ_PROTOCOL_PROFILE_RESULT_PAYLOAD_SIZE : 0u);
    if (succeeded && s_flash_action == FLASH_ACTION_SAVE_CROSSFEED) {
      hid_response_crossfeed(s_flash_request_opcode, s_flash_request_id);
    } else if (succeeded && s_flash_action == FLASH_ACTION_SAVE_OUTPUT_PROCESSING) {
      hid_response_output_processing(s_flash_request_opcode, s_flash_request_id);
    } else if (succeeded) {
      eq_profile_state_t state;
      eq_settings_get_profile_state(&state);
      uint8_t *payload = &s_hid_response[EQ_PROTOCOL_HEADER_SIZE];
      payload[0] = s_flash_profile_index;
      eq_protocol_write_u32(payload + 4u, s_flash_action == FLASH_ACTION_SAVE_PROFILE
                                              ? generation
                                              : state.bank_generation);
    }
    s_flash_write_pending = false;
  }

  if (s_hid_response_pending && tud_hid_ready() && tud_hid_report(0u, s_hid_response, sizeof(s_hid_response))) {
    s_hid_response_pending = false;
    if (s_device_reset_action != DEVICE_RESET_NONE) {
      s_device_reset_response_in_flight = true;
      s_device_reset_deadline_us = time_us_64() + DEVICE_RESET_FALLBACK_DELAY_US;
    }
  }
}

void tud_hid_report_complete_cb(uint8_t instance, uint8_t const *report, uint16_t len) {
  (void)instance;
  (void)report;
  (void)len;
  hid_activity_notify(&s_hid_activity, time_us_32());
  if (!s_device_reset_response_in_flight) return;
  s_device_reset_response_in_flight = false;
  s_device_reset_deadline_us = time_us_64() + DEVICE_RESET_ACK_DELAY_US;
}

void tud_umount_cb(void) {
  set_streaming_state(false);
  hid_activity_reset(&s_hid_activity);
  s_hid_response_pending = false;
  s_meter_configured = false;
  s_meter_active = false;
}

static void device_reset_task(void) {
  if (s_device_reset_action == DEVICE_RESET_NONE || s_device_reset_deadline_us == 0u ||
      time_us_64() < s_device_reset_deadline_us) return;

  device_reset_action_t action = s_device_reset_action;
  s_device_reset_action = DEVICE_RESET_NONE;
  if (action == DEVICE_RESET_BOOTSEL) reset_usb_boot(0u, 0u);

  watchdog_reboot(0u, 0u, 0u);
  while (true) tight_loop_contents();
}

static void meter_stream_task(void) {
  if (!s_meter_active) return;

  uint64_t now = time_us_64();
  if (now >= s_meter_deadline_us) {
    s_meter_active = false;
    s_meter_next_report_us = 0u;
    meter_reset();
    return;
  }
  if (now < s_meter_next_report_us || s_hid_response_pending || !tud_hid_ready()) return;

  meter_accumulator_t meter = meter_take();

  uint32_t pre_eq_left_mean_square = 0u;
  uint32_t pre_eq_right_mean_square = 0u;
  uint32_t post_eq_left_mean_square = 0u;
  uint32_t post_eq_right_mean_square = 0u;
  if (meter.frame_count != 0u) {
    pre_eq_left_mean_square = (uint32_t)(meter.pre_eq.left_square_sum / meter.frame_count);
    pre_eq_right_mean_square = (uint32_t)(meter.pre_eq.right_square_sum / meter.frame_count);
    post_eq_left_mean_square = (uint32_t)(meter.post_eq.left_square_sum / meter.frame_count);
    post_eq_right_mean_square = (uint32_t)(meter.post_eq.right_square_sum / meter.frame_count);
  }

  uint8_t report[EQ_PROTOCOL_REPORT_SIZE];
  eq_protocol_response_init(report, EQ_OPCODE_METER_LEVEL, 0u, EQ_STATUS_OK,
                            EQ_PROTOCOL_METER_LEVEL_PAYLOAD_SIZE);
  uint8_t *payload = &report[EQ_PROTOCOL_HEADER_SIZE];
  eq_protocol_write_u32(payload, ++s_meter_sequence);
  eq_protocol_write_u16(payload + 4u, meter.pre_eq.left_peak);
  eq_protocol_write_u16(payload + 6u, meter.pre_eq.right_peak);
  eq_protocol_write_u32(payload + 8u, pre_eq_left_mean_square);
  eq_protocol_write_u32(payload + 12u, pre_eq_right_mean_square);
  eq_protocol_write_u16(payload + 16u, meter.post_eq.left_peak);
  eq_protocol_write_u16(payload + 18u, meter.post_eq.right_peak);
  eq_protocol_write_u32(payload + 20u, post_eq_left_mean_square);
  eq_protocol_write_u32(payload + 24u, post_eq_right_mean_square);
  payload[28] = meter.limiter_active ? EQ_PROTOCOL_METER_FLAG_LIMITER_ACTIVE : 0u;

  s_meter_next_report_us = now + (uint64_t)s_meter_report_interval_ms * 1000u;
  (void)tud_hid_report(0u, report, sizeof(report));
}

void tud_audio_feedback_params_cb(uint8_t func_id, uint8_t alt_itf, audio_feedback_params_t *feedback_param) {
  (void) func_id;
  (void) alt_itf;
  feedback_param->method = AUDIO_FEEDBACK_METHOD_DISABLED;
  feedback_param->sample_freq = s_sample_rate_hz;
}

bool tud_audio_feedback_format_correction_cb(uint8_t func_id) {
  (void)func_id;
  return tud_speed_get() == TUSB_SPEED_FULL && quirk_os_guessing_get() == QUIRK_OS_GUESSING_OSX;
}

static void audio_feedback_task(void) {
  if (!s_streaming_active) return;

  uint64_t now = time_us_64();
  if (!s_feedback_initialized) {
    audio_feedback_init(&s_feedback_controller, s_sample_rate_hz);
    (void)tud_audio_n_fb_set(0u, s_feedback_controller.nominal_q16);
    s_feedback_initialized = true;
    s_feedback_next_update_us = now + 1000u;
    return;
  }
  if (now < s_feedback_next_update_us) return;

  uint32_t feedback = audio_feedback_update(&s_feedback_controller, i2s_out_buffered_frames());
  (void)tud_audio_n_fb_set(0u, feedback);
  s_feedback_next_update_us = now + 1000u;
}

static void audio_block_release(i2s_audio_block_t *block) {
  bool added = queue_try_add(&s_free_audio_blocks, &block);
  hard_assert(added);
}

int main(void) {
  board_init();

  adc_init();
  adc_set_temp_sensor_enabled(true);
  adc_select_input(ADC_TEMPERATURE_CHANNEL_NUM);

  queue_init(&s_free_audio_blocks, sizeof(audio_block_t *), AUDIO_BLOCK_COUNT);
  queue_init(&s_pending_audio_blocks, sizeof(audio_block_t *), AUDIO_BLOCK_COUNT);
  for (uint32_t i = 0; i < AUDIO_BLOCK_COUNT; i++) {
    audio_block_t *block = &s_audio_blocks[i];
    queue_add_blocking(&s_free_audio_blocks, &block);
  }

  eq_settings_core_init();
  uint32_t loaded_generation = 1u;
  eq_config_t initial_config;
  if (!eq_settings_load(&initial_config, &loaded_generation)) {
    eq_config_set_defaults(&initial_config);
    loaded_generation = 1u;
  }
  eq_settings_get_crossfeed(&s_crossfeed_live);
  s_crossfeed_saved = s_crossfeed_live;
  eq_settings_get_output_processing(&s_output_live);
  s_output_saved = s_output_live;
  s_saved_config = initial_config;
  s_config_generation = loaded_generation;
  s_saved_generation = loaded_generation;
  eq_profile_state_t profile_state;
  eq_settings_get_profile_state(&profile_state);
  s_active_profile = profile_state.default_profile;
  s_persisted_profile = profile_state.default_profile;

  for (uint32_t i = 0; i < 2u; ++i) {
    s_config_snapshots[i].config = initial_config;
    s_config_snapshots[i].generation = loaded_generation;
    s_controls_snapshots[i].crossfeed = s_crossfeed_live;
    s_controls_snapshots[i].output = s_output_live;
    s_controls_snapshots[i].gains[0] = 1.0f;
    s_controls_snapshots[i].gains[1] = 1.0f;
    s_controls_snapshots[i].generation = s_audio_controls_generation;
  }

  multicore_launch_core1(dsp_core_main);
  while (!s_dsp_core_ready) tight_loop_contents();

  led_pwm_init(LED_RED_PIN);
  led_pwm_init(LED_BLUE_PIN);
  red_led_set(false);
  led_set_level(LED_BLUE_PIN, 0u);

  i2s_out_init(s_sample_rate_hz, s_audio_format, audio_block_release);

  tud_init(BOARD_TUD_RHPORT);
  board_init_after_tusb();

  memset(s_mute, 0, sizeof(s_mute));
  memset(s_volume_q8, 0, sizeof(s_volume_q8));

  while (true) {
    tud_task();
    device_reset_task();
    hid_control_task();
    meter_stream_task();
    red_led_set(hid_activity_level(&s_hid_activity, time_us_32(), s_streaming_active));
    audio_task();
    audio_feedback_task();
  }
}
