#include "reference.h"
#include <assert.h>
#include <math.h>
int main(void) {
  uint32_t rates[]={44100,48000,88200,96000,176400,192000};
  for(unsigned r=0;r<6;++r) for(unsigned f=0;f<BENCH_FIXTURES;++f) {
    eq_config_t cfg;bench_fixture(&cfg,f);assert(eq_config_validate(&cfg));
    bench_coeff_t c[10];bench_ref_stage_t s[10];
    for(unsigned i=0;i<10;++i) assert(bench_coefficients(&c[i],&cfg.filters[i],rates[r]));
    bench_reference_reset(s,c,10);
    double peak=0;
    for(unsigned n=0;n<10000;++n) {
      double v=bench_reference_sample(s,10,0,n==0?1.0:0.0);
      assert(isfinite(v));if(f==1)assert(v==(n==0?1:0));
      if(fabs(v)>peak)peak=fabs(v);
    }
    assert(isfinite(peak));
  }
  eq_config_t cfg;bench_fixture(&cfg,2);bench_coeff_t c;
  assert(bench_coefficients(&c,&cfg.filters[0],96000));
  double w=6.283185307179586*20/96000;
  double nr=c.b0+c.b1*cos(w)+c.b2*cos(2*w),ni=-c.b1*sin(w)-c.b2*sin(2*w);
  double dr=1+c.a1*cos(w)+c.a2*cos(2*w),di=-c.a1*sin(w)-c.a2*sin(2*w);
  assert(fabs(10*log10((nr*nr+ni*ni)/(dr*dr+di*di))-12)<1e-6);
}
