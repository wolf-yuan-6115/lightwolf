#include "cmsis_eq.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static bench_cmsis_t state;
static int32_t input[388],output[388];
int main(void){
 unsigned rates[]={96000,192000},fixtures[]={0,2};
 puts("fixture,rate,headroom,tail_rms_dbfs,tail_peak_dbfs");
 for(unsigned r=0;r<2;++r)for(unsigned f=0;f<2;++f){
  eq_config_t cfg;bench_fixture(&cfg,fixtures[f]);bench_coeff_t c[10];
  for(unsigned i=0;i<10;++i)bench_coefficients(&c[i],&cfg.filters[i],rates[r]);
  assert(bench_cmsis_init(&state,c));double sum=0,peak=0;
  for(unsigned pos=0;pos<rates[r]*10;pos+=194){
   unsigned frames=rates[r]*10-pos<194?rates[r]*10-pos:194;
   memset(input,0,sizeof(input));if(!pos)input[0]=134217728;
   assert(bench_cmsis_process(&state,input,output,frames));
   for(unsigned n=0;n<frames;++n)if(pos+n>=rates[r]*9){double x=output[2*n]/268435456.0;sum+=x*x;if(fabs(x)>peak)peak=fabs(x);}
  }
  printf("%s,%u,3,%.5f,%.5f\n",bench_fixture_name(fixtures[f]),rates[r],20*log10(fmax(sqrt(sum/rates[r]),1e-30)),20*log10(fmax(peak,1e-30)));
 }
}
