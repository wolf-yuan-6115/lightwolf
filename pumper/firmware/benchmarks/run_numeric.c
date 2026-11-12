#include "fixed.h"
#include "eq_dsp.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#define N 16384u
static int32_t input[N];static double ideal[N];static float f32[N*2];
// Link the unmodified production EQ with only its downstream controls removed.
bool bench_no_controls(float *l,float *r){(void)l;(void)r;return false;}
static double response(bench_coeff_t c,double w){
  double nr=c.b0+c.b1*cos(w)+c.b2*cos(2*w),ni=-c.b1*sin(w)-c.b2*sin(2*w);
  double dr=1+c.a1*cos(w)+c.a2*cos(2*w),di=-c.a1*sin(w)-c.a2*sin(2*w);
  return sqrt((nr*nr+ni*ni)/(dr*dr+di*di));
}
static double db(double x){return 20*log10(fmax(x,1e-30));}
int main(int argc,char **argv){
  bool quick=argc>1; (void)argv;
  uint32_t rates[]={44100,48000,88200,96000,176400,192000};
  double levels[]={-6,-60,-120};
  printf("variant,fixture,rate,signal,dbfs,headroom,overflow,clip,unstable,response_error_db,notch_error_abs,rms_error_dbfs,arithmetic_error_dbfs,float_error_dbfs,improvement_db\n");
  for(unsigned ri=0;ri<6;++ri)for(unsigned fi=0;fi<BENCH_FIXTURES;++fi){
    if(quick && (ri!=3 || fi>2))continue;
    uint32_t rate=rates[ri];eq_config_t cfg;bench_fixture(&cfg,fi);
    bench_coeff_t c[10];for(unsigned j=0;j<10;++j)bench_coefficients(&c[j],&cfg.filters[j],rate);
    for(unsigned sig=0;sig<8;++sig)for(unsigned lev=0;lev<3;++lev){
      uint32_t seed=0x93a46f21;double amplitude=pow(10,levels[lev]/20);
      bench_ref_stage_t ref[10];bench_reference_reset(ref,c,10);
      eq_init(rate,&cfg);
      for(unsigned n=0;n<N;++n){
        input[n]=(int32_t)round(bench_signal(n,rate,sig,&seed)*amplitude);
        ideal[n]=bench_reference_sample(ref,10,0,input[n]/2147483648.0);
        f32[2*n]=f32[2*n+1]=(float)input[n]/65536.0f;
      }
      eq_process_interleaved_stereo(f32,N,NULL,false);
      double ferr=0;for(unsigned n=0;n<N;++n){double e=f32[2*n]/32768.0-ideal[n];ferr+=e*e;}
      ferr=sqrt(ferr/N);
      for(unsigned wide=0;wide<2;++wide){
        bench_fixed_t fixed;unsigned h;
        for(h=0;h<=30;++h){
          if(!bench_fixed_init(&fixed,c,wide))break;
          for(unsigned n=0;n<N;++n){
            bench_fixed_sample(&fixed,0,(int32_t)bench_round_shift(input[n],h));
            if(fixed.overflows||fixed.clips)break;
          }
          if(!fixed.overflows&&!fixed.clips)break;
        }
        if(h>30)h=30;
        bool failed=fixed.overflows||fixed.clips||fixed.unstable;
        double rms=0,arithmetic=0,resp=0,notch=0;
        if(!failed){
          bench_fixed_init(&fixed,c,wide);bench_coeff_t qc[10];bench_fixed_coefficients(&fixed,qc);
          for(unsigned k=0;k<=1024;++k){
            double hz=20*pow(1000.0,k/1024.0),w=6.283185307179586*hz/rate;
            double a=1,b=1;for(unsigned j=0;j<10;++j){a*=response(c[j],w);b*=response(qc[j],w);}
            if(a>1e-4){double e=fabs(db(b)-db(a));if(e>resp)resp=e;}
            else if(fabs(b-a)>notch)notch=fabs(b-a);
          }
          bench_reference_reset(ref,qc,10);
          for(unsigned n=0;n<N;++n){
            double quant_ref=bench_reference_sample(ref,10,0,input[n]/2147483648.0);
            double out=ldexp((double)bench_fixed_sample(&fixed,0,(int32_t)bench_round_shift(input[n],h)),(int)h-31);
            double e=out-ideal[n],a=out-quant_ref;rms+=e*e;arithmetic+=a*a;
          }
          rms=sqrt(rms/N);arithmetic=sqrt(arithmetic/N);
        }
        printf("%s,%s,%u,%u,%.0f,%u,%u,%u,%u,%.9g,%.9g,%.5f,%.5f,%.5f,%.5f\n",wide?"q31-state64":"q31-state32",bench_fixture_name(fi),rate,sig,levels[lev],h,fixed.overflows,fixed.clips,fixed.unstable,resp,notch,failed?NAN:db(rms),failed?NAN:db(arithmetic),db(ferr),failed?NAN:db(ferr)-db(rms));
      }
    }
  }
}
