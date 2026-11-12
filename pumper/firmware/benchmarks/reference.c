#include "reference.h"
#include <math.h>
#include <string.h>
#define EQ_PI 3.14159265358979323846
static bench_coeff_t const k_identity = {1,0,0,0,0};

// Independent double-precision RBJ reference, derived from the production formulas.
bool bench_coefficients(bench_coeff_t *out, eq_filter_config_t const *cfg, double fs) {
  bool gain_filter = cfg->type <= EQ_FILTER_HIGH_SHELF;
  if (!cfg->enabled || (gain_filter && fabs(cfg->gain_db) < 0.0001) ||
      cfg->frequency_hz >= fs * 0.5) {
    *out = k_identity;
    return true;
  }

  double w0 = 2.0 * EQ_PI * cfg->frequency_hz / fs;
  double sin_w0 = sin(w0);
  double cos_w0 = cos(w0);
  double a = pow(10.0, cfg->gain_db / 40.0);
  double b0;
  double b1;
  double b2;
  double a0;
  double a1;
  double a2;

  if (cfg->type == EQ_FILTER_PEAKING) {
    double alpha;
    if (cfg->width_mode == EQ_WIDTH_BANDWIDTH) {
      if (fabs(sin_w0) < 1e-12) return false;
      alpha = sin_w0 * sinh((log(2.0) * 0.5) * cfg->bw_octaves * (w0 / sin_w0));
    } else {
      alpha = sin_w0 / (2.0 * cfg->q);
    }
    b0 = 1.0 + alpha * a;
    b1 = -2.0 * cos_w0;
    b2 = 1.0 - alpha * a;
    a0 = 1.0 + alpha / a;
    a1 = -2.0 * cos_w0;
    a2 = 1.0 - alpha / a;
  } else if (cfg->type == EQ_FILTER_LOW_SHELF || cfg->type == EQ_FILTER_HIGH_SHELF) {
    double slope = cfg->q;
    double alpha = sin_w0 * 0.5 * sqrt((a + (1.0 / a)) * ((1.0 / slope) - 1.0) + 2.0);
    double two_sqrt_a_alpha = 2.0 * sqrt(a) * alpha;
    if (cfg->type == EQ_FILTER_LOW_SHELF) {
      b0 = a * ((a + 1.0) - (a - 1.0) * cos_w0 + two_sqrt_a_alpha);
      b1 = 2.0 * a * ((a - 1.0) - (a + 1.0) * cos_w0);
      b2 = a * ((a + 1.0) - (a - 1.0) * cos_w0 - two_sqrt_a_alpha);
      a0 = (a + 1.0) + (a - 1.0) * cos_w0 + two_sqrt_a_alpha;
      a1 = -2.0 * ((a - 1.0) + (a + 1.0) * cos_w0);
      a2 = (a + 1.0) + (a - 1.0) * cos_w0 - two_sqrt_a_alpha;
    } else {
      b0 = a * ((a + 1.0) + (a - 1.0) * cos_w0 + two_sqrt_a_alpha);
      b1 = -2.0 * a * ((a - 1.0) + (a + 1.0) * cos_w0);
      b2 = a * ((a + 1.0) + (a - 1.0) * cos_w0 - two_sqrt_a_alpha);
      a0 = (a + 1.0) - (a - 1.0) * cos_w0 + two_sqrt_a_alpha;
      a1 = 2.0 * ((a - 1.0) - (a + 1.0) * cos_w0);
      a2 = (a + 1.0) - (a - 1.0) * cos_w0 - two_sqrt_a_alpha;
    }
  } else {
    double alpha = sin_w0 / (2.0 * cfg->q);
    a0 = 1.0 + alpha;
    a1 = -2.0 * cos_w0;
    a2 = 1.0 - alpha;
    if (cfg->type == EQ_FILTER_LOW_PASS) {
      b0 = (1.0 - cos_w0) * 0.5;
      b1 = 1.0 - cos_w0;
      b2 = b0;
    } else if (cfg->type == EQ_FILTER_HIGH_PASS) {
      b0 = (1.0 + cos_w0) * 0.5;
      b1 = -(1.0 + cos_w0);
      b2 = b0;
    } else if (cfg->type == EQ_FILTER_NOTCH) {
      b0 = 1.0;
      b1 = -2.0 * cos_w0;
      b2 = 1.0;
    } else {
      b0 = alpha;
      b1 = 0.0;
      b2 = -alpha;
    }
  }

  if (fabs(a0) < 1e-12) return false;
  *out = (bench_coeff_t){b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
  return true;
}


void bench_reference_reset(bench_ref_stage_t *s, bench_coeff_t const *c, size_t stages) {
  memset(s, 0, stages * sizeof(*s));
  for (size_t i=0; i<stages; ++i) s[i].c=c[i];
}
double bench_reference_sample(bench_ref_stage_t *s, size_t stages, unsigned ch, double x) {
  for (size_t i=0; i<stages; ++i) {
    bench_ref_stage_t *p=&s[i]; bench_coeff_t c=p->c;
    double y=c.b0*x+c.b1*p->x[ch][0]+c.b2*p->x[ch][1]-c.a1*p->y[ch][0]-c.a2*p->y[ch][1];
    p->x[ch][1]=p->x[ch][0];p->x[ch][0]=x;
    p->y[ch][1]=p->y[ch][0];p->y[ch][0]=y;x=y;
  }
  return x;
}
