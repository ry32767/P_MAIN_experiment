#pragma once
#include <AquaBeacon.h>
#include <stdio.h>
#include <math.h>
namespace aqimu {
constexpr const char csvHeader[]="seq,received_mono_us,sensor_timestamp_raw,temp,gx,gy,gz,ax,ay,az,corrected_gx,corrected_gy,corrected_gz,corrected_ax,corrected_ay,corrected_az,calibration_generation,sensor_ticks_unwrapped,sample_mono_us,sample_utc_us,clock_generation,pps_count,parent_set_generation,parent_session,time_quality,time_uncertainty_us,crc32\n";
constexpr uint32_t rate = 960;
constexpr unsigned batchSize = 32;
constexpr uint32_t magic = 0x494d5533;
struct Data { uint32_t timestamp; float temp,gx,gy,gz,ax,ay,az; };
struct Row { uint64_t received_us; uint32_t seq; Data data; float corrected[6]; uint32_t calibration; uint64_t sensor_ticks=0,sample_mono_us=0,sample_utc_us=0; uint32_t clock_generation=0,pps_count=0,set_generation=0,parent_session=0; uint8_t time_quality=0; };
struct Batch {
  uint32_t version, id, count, length, error;
  Row rows[batchSize];
  char csv[batchSize*640];
};
// Identical layout on MainCore/SubCore; MP messages transfer ownership.
// Avoid NuttX printf's expensive floating-point conversion on every sample.
// Convert the exact float promoted to double, rounding to the existing CSV precision.
inline bool appendFixed(char* out,size_t capacity,int& used,float value,unsigned decimals){
 if(!isfinite(value) || fabs(double(value))>=1e9 || (decimals!=6 && decimals!=8))return false;
 uint64_t scale=decimals==6?1000000ULL:100000000ULL;
 uint64_t rounded=uint64_t(fabs(double(value))*double(scale)+0.5);
 char digits[24];unsigned count=0;uint64_t whole=rounded/scale;
 do{digits[count++]=char('0'+whole%10);whole/=10;}while(whole);
 unsigned needed=1+(signbit(value)?1:0)+count+1+decimals;
 if(size_t(used)+needed>=capacity)return false;
 out[used++]=',';if(signbit(value))out[used++]='-';
 while(count)out[used++]=digits[--count];out[used++]='.';
 uint64_t fraction=rounded%scale;
 for(uint64_t divisor=scale/10;divisor;divisor/=10)out[used++]=char('0'+(fraction/divisor)%10);
 out[used]=0;return true;
}
inline bool format(Batch &b) {
  b.length=0; b.error=1;
  if(b.version!=magic || b.count>batchSize || !b.count) return false;
  for(unsigned i=0;i<b.count;++i) {
    const auto &r=b.rows[i]; const auto &d=r.data; char line[640];
    int n=snprintf(line,sizeof(line),"%lu,%llu,%lu",
      (unsigned long)r.seq,(unsigned long long)r.received_us,(unsigned long)d.timestamp);
    if(n<0 || unsigned(n)>=sizeof(line))return false;
    const float values[]={d.temp,d.gx,d.gy,d.gz,d.ax,d.ay,d.az,r.corrected[0],r.corrected[1],r.corrected[2],r.corrected[3],r.corrected[4],r.corrected[5]};
    for(unsigned j=0;j<13;j++)if(!appendFixed(line,sizeof(line),n,values[j],j?8:6))return false;
    int generation=snprintf(line+n,sizeof(line)-n,",%lu",(unsigned long)r.calibration);
    if(generation<0 || unsigned(n)+unsigned(generation)+10>=sizeof(line))return false;
    n+=generation;
    int timing=snprintf(line+n,sizeof(line)-n,",%llu,%llu,%llu,%lu,%lu,%lu,%lu,%u,%lu",
      (unsigned long long)r.sensor_ticks,(unsigned long long)r.sample_mono_us,(unsigned long long)r.sample_utc_us,
      (unsigned long)r.clock_generation,(unsigned long)r.pps_count,(unsigned long)r.set_generation,(unsigned long)r.parent_session,unsigned(r.time_quality),(unsigned long)UINT32_MAX);
    if(timing<0 || unsigned(n)+unsigned(timing)+10>=sizeof(line))return false;n+=timing;
    uint32_t crc=aq::crc32((const uint8_t*)line,n);
    int extra=snprintf(line+n,sizeof(line)-n,",%08lx\n",(unsigned long)crc);
    if(extra!=10 || b.length+n+extra>sizeof(b.csv)) return false;
    memcpy(b.csv+b.length,line,n+extra); b.length+=n+extra;
  }
  b.error=0; return true;
}
}
