#include "fixed.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <string.h>
static int64_t oracle(int32_t a,int64_t b){
  __int128 p=(__int128)a*b;bool neg=p<0;if(neg)p=-p;
  p=(p+((__int128)1<<31))>>32;return neg?-(int64_t)p:(int64_t)p;
}
int main(void){
  int32_t a[]={INT32_MIN,INT32_MAX,-1,0,1,0x12345678};
  int64_t b[]={INT64_MIN,INT64_MAX,-1,0,1,INT64_C(0x123456789abcdef)};
  for(unsigned i=0;i<6;++i)for(unsigned j=0;j<6;++j)assert(bench_mul32x64(a[i],b[j])==oracle(a[i],b[j]));
  uint32_t seed=0x93a46f21;
  for(unsigned i=0;i<100000;++i){
    seed=1664525*seed+1013904223;int32_t x=(int32_t)seed;
    seed=1664525*seed+1013904223;uint64_t y=(uint64_t)seed<<32;
    seed=1664525*seed+1013904223;y|=seed;
    int64_t z;memcpy(&z,&y,8);assert(bench_mul32x64(x,z)==oracle(x,z));
  }
  assert(bench_round_shift(INT64_MIN,63)==-1);
  assert(bench_round_shift(INT64_MAX,63)==1);
  assert(bench_round_shift(-3,1)==-2);
  for(unsigned wide=0;wide<2;++wide){
    bench_coeff_t c[10];for(unsigned i=0;i<10;++i)c[i]=(bench_coeff_t){1,0,0,0,0};
    bench_fixed_t s;assert(bench_fixed_init(&s,c,wide));
    for(unsigned i=0;i<6;++i)assert(bench_fixed_sample(&s,0,a[i])==a[i]);
    c[0].b0=0.25;assert(bench_fixed_ramp(&s,c,960));
    for(unsigned i=0;i<960;++i){
      int32_t l=bench_fixed_sample(&s,0,1000000),r=bench_fixed_sample(&s,1,-1000000);
      assert(l==-r);assert(l>=250000&&l<=1000000);
    }
    assert(bench_fixed_sample(&s,0,1000000)==250000);
    assert(!s.overflows&&!s.clips&&!s.unstable);
    c[0]=(bench_coeff_t){1,0,0,-2,1};assert(!bench_fixed_init(&s,c,wide));
    c[0]=(bench_coeff_t){8,0,0,0,0};assert(bench_fixed_init(&s,c,wide));
    bench_fixed_sample(&s,0,INT32_MAX);assert(s.clips);
  }
}
