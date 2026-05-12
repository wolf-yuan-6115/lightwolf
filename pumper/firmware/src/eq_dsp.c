#include "eq_dsp.h"

#include <math.h>
#include <stdbool.h>

#include "eq_config.h"

#define EQ_PI 3.14159265358979323846f

typedef struct {
  float b0;
  float b1;
  float b2;
  float a1;
  float a2;
  float z1_l;
  float z2_l;
  float z1_r;
  float z2_r;
  bool enabled;
} eq_biquad_t;

static eq_biquad_t s_bands[EQ_NUM_FILTERS];
static uint8_t s_active_band_idx[EQ_NUM_FILTERS];
static uint32_t s_active_band_count = 0u;
static float s_preamp_linear = 1.0f;
static uint32_t s_sample_rate_hz = 48000u;

static float db_to_linear(float gain_db) {
  return powf(10.0f, gain_db / 20.0f);
}

static float eq_process_one(float x, eq_biquad_t *band, bool right_channel) {
  if (!band->enabled) {
    return x;
  }

  float z1 = right_channel ? band->z1_r : band->z1_l;
  float z2 = right_channel ? band->z2_r : band->z2_l;
  float y = band->b0 * x + z1;
  float new_z1 = band->b1 * x - band->a1 * y + z2;
  float new_z2 = band->b2 * x - band->a2 * y;

  if (right_channel) {
    band->z1_r = new_z1;
    band->z2_r = new_z2;
  } else {
    band->z1_l = new_z1;
    band->z2_l = new_z2;
  }

  return y;
}

static bool eq_build_biquad_coefficients(eq_biquad_t *band, eq_filter_config_t const *cfg, float fs, float nyquist) {
  if (cfg->frequency_hz <= 0.0f || cfg->frequency_hz >= nyquist) {
    return false;
  }

  float gain_db = cfg->gain_db;
  if (fabsf(gain_db) < 0.0001f) {
    return false;
  }

  float w0 = 2.0f * EQ_PI * cfg->frequency_hz / fs;
  float sin_w0 = sinf(w0);
  float cos_w0 = cosf(w0);
  float a = powf(10.0f, gain_db / 40.0f);
  float b0;
  float b1;
  float b2;
  float a0;
  float a1;
  float a2;

  if (cfg->type == EQ_FILTER_PEAKING) {
    float alpha;
    if (cfg->bw_octaves > 0.0f) {
      if (fabsf(sin_w0) < 1e-12f) {
        return false;
      }
      alpha = sin_w0 * sinhf((logf(2.0f) * 0.5f) * cfg->bw_octaves * (w0 / sin_w0));
    } else {
      float q = cfg->q;
      if (q <= 0.0f) {
        return false;
      }
      alpha = sin_w0 / (2.0f * q);
    }
    b0 = 1.0f + alpha * a;
    b1 = -2.0f * cos_w0;
    b2 = 1.0f - alpha * a;
    a0 = 1.0f + alpha / a;
    a1 = -2.0f * cos_w0;
    a2 = 1.0f - alpha / a;
  } else if (cfg->type == EQ_FILTER_LOW_SHELF || cfg->type == EQ_FILTER_HIGH_SHELF) {
    float shelf_s = cfg->q;
    if (shelf_s <= 0.0f) {
      return false;
    }
    float alpha = sin_w0 * 0.5f * sqrtf((a + (1.0f / a)) * ((1.0f / shelf_s) - 1.0f) + 2.0f);
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
  } else {
    return false;
  }

  if (fabsf(a0) < 1e-12f) {
    return false;
  }

  band->b0 = b0 / a0;
  band->b1 = b1 / a0;
  band->b2 = b2 / a0;
  band->a1 = a1 / a0;
  band->a2 = a2 / a0;
  return true;
}

static void eq_reset_state(void) {
  for (uint32_t i = 0; i < EQ_NUM_FILTERS; i++) {
    s_bands[i].z1_l = 0.0f;
    s_bands[i].z2_l = 0.0f;
    s_bands[i].z1_r = 0.0f;
    s_bands[i].z2_r = 0.0f;
  }
}

static void eq_rebuild_coefficients(void) {
  if (!EQ_ENABLED) {
    s_preamp_linear = 1.0f;
    s_active_band_count = 0u;
    eq_reset_state();
    return;
  }

  s_preamp_linear = db_to_linear(EQ_PREAMP_DB);
  s_active_band_count = 0u;

  float fs = (float) s_sample_rate_hz;
  float nyquist = fs * 0.5f;

  for (uint32_t i = 0; i < EQ_NUM_FILTERS; i++) {
    eq_biquad_t *band = &s_bands[i];
    band->enabled = false;
    band->b0 = 1.0f;
    band->b1 = 0.0f;
    band->b2 = 0.0f;
    band->a1 = 0.0f;
    band->a2 = 0.0f;

    if (!eq_build_biquad_coefficients(band, &k_eq_filters[i], fs, nyquist)) {
      continue;
    }
    band->enabled = true;
    s_active_band_idx[s_active_band_count++] = (uint8_t) i;
  }

  eq_reset_state();
}

void eq_init(uint32_t sample_rate_hz) {
  eq_set_sample_rate(sample_rate_hz);
}

void eq_set_sample_rate(uint32_t sample_rate_hz) {
  if (sample_rate_hz == 0u) {
    return;
  }

  s_sample_rate_hz = sample_rate_hz;
  eq_rebuild_coefficients();
}

void eq_process_interleaved_stereo16(int16_t *interleaved, size_t frame_count) {
  if (!EQ_ENABLED) {
    return;
  }
  if (interleaved == NULL || frame_count == 0u) {
    return;
  }
  if (s_active_band_count == 0u && fabsf(s_preamp_linear - 1.0f) < 0.0001f) {
    return;
  }

  for (size_t frame = 0; frame < frame_count; frame++) {
    float left = (float) interleaved[frame * 2u] * s_preamp_linear;
    float right = (float) interleaved[frame * 2u + 1u] * s_preamp_linear;

    for (uint32_t i = 0; i < s_active_band_count; i++) {
      eq_biquad_t *band = &s_bands[s_active_band_idx[i]];
      left = eq_process_one(left, band, false);
      right = eq_process_one(right, band, true);
    }

    if (left > 32767.0f) left = 32767.0f;
    if (left < -32768.0f) left = -32768.0f;
    if (right > 32767.0f) right = 32767.0f;
    if (right < -32768.0f) right = -32768.0f;

    interleaved[frame * 2u] = (int16_t) lrintf(left);
    interleaved[frame * 2u + 1u] = (int16_t) lrintf(right);
  }
}
