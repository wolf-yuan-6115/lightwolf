#include "reference.h"
#include <limits.h>
#include <math.h>
static eq_filter_config_t const captured[EQ_NUM_FILTERS] = {
    {true, 1, 1, 68.0f, 0.71f, 1.89f, -4.9f},
    {true, 0, 0, 105.0f, 0.71f, 1.89f, 5.5f},
    {true, 1, 1, 260.0f, 4.0f, 0.36f, -1.0f},
    {true, 1, 1, 1300.0f, 3.0f, 0.48f, 1.3f},
    {true, 1, 1, 1650.0f, 3.0f, 0.48f, -2.7f},
    {true, 1, 1, 2600.0f, 5.0f, 0.29f, -1.0f},
    {true, 2, 0, 3000.0f, 0.35f, 3.33f, 1.0f},
    {true, 1, 1, 3000.0f, 1.4f, 1.01f, 2.0f},
    {true, 1, 1, 5100.0f, 4.5f, 0.32f, -5.0f},
    {true, 2, 0, 10000.0f, 0.71f, 1.89f, -1.0f},
};
char const *bench_fixture_name(unsigned n) {
  static char const *names[]={"current-preset","flat","low20-q20","boost-stack","cut-stack","mixed-filters","boundary-filters"};
  return n<BENCH_FIXTURES?names[n]:"invalid";
}
void bench_fixture(eq_config_t *c, unsigned n) {
  *c=k_eq_default_config;
  for (unsigned i=0;i<EQ_NUM_FILTERS;++i) {
    eq_filter_config_t *f=&c->filters[i];
    if(n==0) *f=captured[i];
    if(n==1) f->enabled=false;
    if(n==2) *f=(eq_filter_config_t){i==0,EQ_FILTER_PEAKING,EQ_WIDTH_Q,20,20,1,12};
    if(n==3||n==4) *f=(eq_filter_config_t){true,EQ_FILTER_PEAKING,EQ_WIDTH_Q,1000,1,1,n==3?24:-24};
    if(n==5) *f=(eq_filter_config_t){true,(eq_filter_type_t)(i%7),EQ_WIDTH_Q,1000+500*i,0.707f,1,(i%7)<3?3:0};
    if(n==6) *f=(eq_filter_config_t){true,(eq_filter_type_t)(i%7),EQ_WIDTH_Q,i%2?20000:20,(i%7==0||i%7==2)?0.1f:(i%2?20:0.1f),1,(i%7)<3?-24:0};
  }
}
int32_t bench_signal(uint32_t n, uint32_t rate, unsigned kind, uint32_t *seed) {
  double x=0;
  switch(kind) {
    case 0: x=sin(6.283185307179586*997*n/rate);break;
    case 1: *seed^=*seed<<13;*seed^=*seed>>17;*seed^=*seed<<5;return (int32_t)(*seed&INT32_MAX)-1073741824;
    case 2: return n==0?INT32_MAX:0;
    case 3: return n<rate/8?INT32_MAX:0;
    case 4: x=(sin(6.283185307179586*20*n/rate)+sin(6.283185307179586*1000*n/rate)+sin(6.283185307179586*19000*n/rate))/3;break;
    case 5: x=sin(6.283185307179586*(20.0*n/rate+9990.0*n*n/((double)rate*rate)));break;
    case 6: return n&1?INT32_MIN:INT32_MAX;
    case 7: return 16777216+(int32_t)(n%3);
    default: return 0;
  }
  return (int32_t)(x*2147483647.0);
}
