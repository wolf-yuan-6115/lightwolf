#include "eq_dsp.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

#define EQ_PI 3.14159265358979323846f
#define EQ_TRANSITION_MS 10u

typedef struct {
  float b0;
  float b1;
  float b2;
  float a1;
  float a2;
} eq_coefficients_t;

typedef struct {
  eq_coefficients_t current;
  eq_coefficients_t target;
  eq_coefficients_t delta;
  float z1_l;
  float z2_l;
  float z1_r;
  float z2_r;
} eq_biquad_t;

static eq_biquad_t s_bands[EQ_NUM_FILTERS];
static uint8_t s_active_bands[EQ_NUM_FILTERS];
static uint8_t s_active_band_count = 0u;
static eq_config_t s_config;
static float s_preamp_current = 1.0f;
static float s_preamp_target = 1.0f;
static float s_preamp_delta = 0.0f;
static uint32_t s_transition_frames = 1u;
static uint32_t s_transition_remaining = 0u;
static uint32_t s_sample_rate_hz = 48000u;

static eq_coefficients_t const k_identity = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f};

static bool coefficients_are_identity(eq_coefficients_t const *coefficients) {
  return coefficients->b0 == 1.0f && coefficients->b1 == 0.0f && coefficients->b2 == 0.0f &&
         coefficients->a1 == 0.0f && coefficients->a2 == 0.0f;
}

static void rebuild_active_bands(bool include_current) {
  s_active_band_count = 0u;
  for (uint8_t i = 0u; i < EQ_NUM_FILTERS; i++) {
    if (!coefficients_are_identity(&s_bands[i].target) ||
        (include_current && !coefficients_are_identity(&s_bands[i].current))) {
      s_active_bands[s_active_band_count++] = i;
    }
  }
}

static float db_to_linear(float gain_db) {
  return powf(10.0f, gain_db / 20.0f);
}

static float process_one(float x, eq_biquad_t *band, bool right_channel) {
  float z1 = right_channel ? band->z1_r : band->z1_l;
  float z2 = right_channel ? band->z2_r : band->z2_l;
  float y = band->current.b0 * x + z1;
  float new_z1 = band->current.b1 * x - band->current.a1 * y + z2;
  float new_z2 = band->current.b2 * x - band->current.a2 * y;

  if (right_channel) {
    band->z1_r = new_z1;
    band->z2_r = new_z2;
  } else {
    band->z1_l = new_z1;
    band->z2_l = new_z2;
  }
  return y;
}

static bool build_coefficients(eq_coefficients_t *out, eq_filter_config_t const *cfg, float fs) {
  if (!cfg->enabled || fabsf(cfg->gain_db) < 0.0001f || cfg->frequency_hz >= fs * 0.5f) {
    *out = k_identity;
    return true;
  }

  float w0 = 2.0f * EQ_PI * cfg->frequency_hz / fs;
  float sin_w0 = sinf(w0);
  float cos_w0 = cosf(w0);
  float a = powf(10.0f, cfg->gain_db / 40.0f);
  float b0;
  float b1;
  float b2;
  float a0;
  float a1;
  float a2;

  if (cfg->type == EQ_FILTER_PEAKING) {
    float alpha;
    if (cfg->width_mode == EQ_WIDTH_BANDWIDTH) {
      if (fabsf(sin_w0) < 1e-12f) return false;
      alpha = sin_w0 * sinhf((logf(2.0f) * 0.5f) * cfg->bw_octaves * (w0 / sin_w0));
    } else {
      alpha = sin_w0 / (2.0f * cfg->q);
    }
    b0 = 1.0f + alpha * a;
    b1 = -2.0f * cos_w0;
    b2 = 1.0f - alpha * a;
    a0 = 1.0f + alpha / a;
    a1 = -2.0f * cos_w0;
    a2 = 1.0f - alpha / a;
  } else {
    float slope = cfg->q;
    float alpha = sin_w0 * 0.5f * sqrtf((a + (1.0f / a)) * ((1.0f / slope) - 1.0f) + 2.0f);
    float two_sqrt_a_alpha = 2.0f * sqrtf(a) * alpha;
    if (cfg->type == EQ_FILTER_LOW_SHELF) {
      b0 = a * ((a + 1.0f) - (a - 1.0f) * cos_w0 + two_sqrt_a_alpha);
      b1 = 2.0f * a * ((a - 1.0f) - (a + 1.0f) * cos_w0);
      b2 = a * ((a + 1.0f) - (a - 1.0f) * cos_w0 - two_sqrt_a_alpha);
      a0 = (a + 1.0f) + (a - 1.0f) * cos_w0 + two_sqrt_a_alpha;
      a1 = -2.0f * ((a - 1.0f) + (a + 1.0f) * cos_w0);
      a2 = (a + 1.0f) + (a - 1.0f) * cos_w0 - two_sqrt_a_alpha;
    } else {
      b0 = a * ((a + 1.0f) + (a - 1.0f) * cos_w0 + two_sqrt_a_alpha);
      b1 = -2.0f * a * ((a - 1.0f) + (a + 1.0f) * cos_w0);
      b2 = a * ((a + 1.0f) + (a - 1.0f) * cos_w0 - two_sqrt_a_alpha);
      a0 = (a + 1.0f) - (a - 1.0f) * cos_w0 + two_sqrt_a_alpha;
      a1 = 2.0f * ((a - 1.0f) - (a + 1.0f) * cos_w0);
      a2 = (a + 1.0f) - (a - 1.0f) * cos_w0 - two_sqrt_a_alpha;
    }
  }

  if (fabsf(a0) < 1e-12f) return false;
  *out = (eq_coefficients_t){b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
  return true;
}

static void reset_states(void) {
  for (uint32_t i = 0; i < EQ_NUM_FILTERS; i++) {
    s_bands[i].z1_l = 0.0f;
    s_bands[i].z2_l = 0.0f;
    s_bands[i].z1_r = 0.0f;
    s_bands[i].z2_r = 0.0f;
  }
}

static bool rebuild_targets(eq_config_t const *config, bool immediate) {
  eq_coefficients_t next[EQ_NUM_FILTERS];
  for (uint32_t i = 0; i < EQ_NUM_FILTERS; i++) {
    if (!config->enabled) {
      next[i] = k_identity;
    } else if (!build_coefficients(&next[i], &config->filters[i], (float)s_sample_rate_hz)) {
      return false;
    }
  }

  s_config = *config;
  s_preamp_target = config->enabled ? db_to_linear(config->preamp_db) : 1.0f;
  for (uint32_t i = 0; i < EQ_NUM_FILTERS; i++) {
    s_bands[i].target = next[i];
    if (immediate) {
      s_bands[i].current = next[i];
      s_bands[i].delta = (eq_coefficients_t){0};
    } else {
      float scale = 1.0f / (float)s_transition_frames;
      s_bands[i].delta = (eq_coefficients_t){
          (next[i].b0 - s_bands[i].current.b0) * scale,
          (next[i].b1 - s_bands[i].current.b1) * scale,
          (next[i].b2 - s_bands[i].current.b2) * scale,
          (next[i].a1 - s_bands[i].current.a1) * scale,
          (next[i].a2 - s_bands[i].current.a2) * scale,
      };
    }
  }
  if (immediate) {
    s_preamp_current = s_preamp_target;
    s_preamp_delta = 0.0f;
    s_transition_remaining = 0u;
    rebuild_active_bands(false);
  } else {
    s_preamp_delta = (s_preamp_target - s_preamp_current) / (float)s_transition_frames;
    s_transition_remaining = s_transition_frames;
    rebuild_active_bands(true);
  }
  return true;
}

static void advance_transition(void) {
  if (s_transition_remaining == 0u) return;
  s_preamp_current += s_preamp_delta;
  for (uint8_t active = 0u; active < s_active_band_count; active++) {
    eq_biquad_t *band = &s_bands[s_active_bands[active]];
    band->current.b0 += band->delta.b0;
    band->current.b1 += band->delta.b1;
    band->current.b2 += band->delta.b2;
    band->current.a1 += band->delta.a1;
    band->current.a2 += band->delta.a2;
  }
  s_transition_remaining--;
  if (s_transition_remaining == 0u) {
    s_preamp_current = s_preamp_target;
    for (uint32_t i = 0u; i < EQ_NUM_FILTERS; i++) s_bands[i].current = s_bands[i].target;
    rebuild_active_bands(false);
  }
}

void eq_init(uint32_t sample_rate_hz, eq_config_t const *config) {
  s_sample_rate_hz = sample_rate_hz == 0u ? 48000u : sample_rate_hz;
  s_transition_frames = (s_sample_rate_hz * EQ_TRANSITION_MS) / 1000u;
  if (s_transition_frames == 0u) s_transition_frames = 1u;
  memset(s_bands, 0, sizeof(s_bands));
  for (uint32_t i = 0; i < EQ_NUM_FILTERS; i++) s_bands[i].current = k_identity;
  eq_config_t initial = k_eq_default_config;
  if (config != NULL && eq_config_validate(config)) initial = *config;
  (void)rebuild_targets(&initial, true);
  reset_states();
}

void eq_set_sample_rate(uint32_t sample_rate_hz) {
  if (sample_rate_hz == 0u || sample_rate_hz == s_sample_rate_hz) return;
  s_sample_rate_hz = sample_rate_hz;
  s_transition_frames = (s_sample_rate_hz * EQ_TRANSITION_MS) / 1000u;
  if (s_transition_frames == 0u) s_transition_frames = 1u;
  (void)rebuild_targets(&s_config, true);
  reset_states();
}

bool eq_set_config(eq_config_t const *config) {
  if (!eq_config_validate(config)) return false;
  return rebuild_targets(config, false);
}

static uint32_t sample_magnitude(int16_t sample) {
  int32_t value = sample;
  return (uint32_t)(value < 0 ? -value : value);
}

static void measure_pair(eq_level_metrics_t *level, int16_t left, int16_t right, bool measure_rms) {
  uint32_t left_magnitude = sample_magnitude(left);
  uint32_t right_magnitude = sample_magnitude(right);
  if (left_magnitude > level->left_peak) level->left_peak = (uint16_t)left_magnitude;
  if (right_magnitude > level->right_peak) level->right_peak = (uint16_t)right_magnitude;
  if (measure_rms) {
    level->left_square_sum += (uint64_t)left_magnitude * left_magnitude;
    level->right_square_sum += (uint64_t)right_magnitude * right_magnitude;
  }
}

static int16_t saturating_round(float sample) {
  if (sample >= 32767.0f) return 32767;
  if (sample <= -32768.0f) return -32768;
  return (int16_t)(sample < 0.0f ? sample - 0.5f : sample + 0.5f);
}

void eq_process_interleaved_stereo16(int16_t *interleaved, size_t frame_count,
                                     eq_block_metrics_t *metrics, bool measure_rms) {
  if (interleaved == NULL || frame_count == 0u) return;
  if (metrics != NULL) memset(metrics, 0, sizeof(*metrics));

  if (s_active_band_count == 0u && s_transition_remaining == 0u &&
      s_preamp_current == 1.0f) {
    if (metrics != NULL) {
      for (size_t frame = 0u; frame < frame_count; frame++) {
        measure_pair(&metrics->pre_eq, interleaved[frame * 2u],
                     interleaved[frame * 2u + 1u], measure_rms);
      }
      metrics->post_eq = metrics->pre_eq;
    }
    return;
  }

  for (size_t frame = 0; frame < frame_count; frame++) {
    int16_t input_left = interleaved[frame * 2u];
    int16_t input_right = interleaved[frame * 2u + 1u];
    if (metrics != NULL) measure_pair(&metrics->pre_eq, input_left, input_right, measure_rms);
    advance_transition();
    float left = (float)input_left * s_preamp_current;
    float right = (float)input_right * s_preamp_current;
    for (uint8_t active = 0u; active < s_active_band_count; active++) {
      eq_biquad_t *band = &s_bands[s_active_bands[active]];
      left = process_one(left, band, false);
      right = process_one(right, band, true);
    }
    int16_t output_left = saturating_round(left);
    int16_t output_right = saturating_round(right);
    interleaved[frame * 2u] = output_left;
    interleaved[frame * 2u + 1u] = output_right;
    if (metrics != NULL) measure_pair(&metrics->post_eq, output_left, output_right, measure_rms);
  }
}
