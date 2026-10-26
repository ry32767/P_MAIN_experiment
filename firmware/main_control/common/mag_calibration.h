#pragma once
#include <stdint.h>
#include <math.h>
#include <string.h>
namespace magcal {
constexpr unsigned capacity=1800,minSamples=200;
struct Coefficients {float offset[3]{},scale[3]{1,1,1};bool enabled=false;};
inline bool valid(const Coefficients& c){for(unsigned i=0;i<3;i++)if(!isfinite(c.offset[i])||fabsf(c.offset[i])>400||!isfinite(c.scale[i])||c.scale[i]<0.25f||c.scale[i]>4)return false;return true;}
inline void apply(const Coefficients& c,const float* raw,float* corrected){for(unsigned i=0;i<3;i++)corrected[i]=c.enabled?(raw[i]-c.offset[i])*c.scale[i]:raw[i];}
struct Result {Coefficients coefficients;uint32_t octants[8]{};float span[3]{},rms=0,meanRadius=0;bool ready=false,fitEvaluated=false,radiusMatch=false;};
class Calibration {
 float points[capacity][3]{};
public:
 bool active=false,saved=false;uint32_t samples=0,rejected=0,saturated=0,startedMs=0,lastMs=0;
 void start(uint32_t now){active=true;saved=false;samples=rejected=saturated=0;startedMs=lastMs=now;}
 void end(){active=false;}
 void add(const float* raw,bool clipped,uint32_t now){
  if(!active)return;
  if(clipped){++saturated;++rejected;return;}
  for(unsigned i=0;i<3;i++)if(!isfinite(raw[i])){++rejected;return;}
  if(samples==capacity){++rejected;return;}
  memcpy(points[samples++],raw,3*sizeof(float));lastMs=now;
 }
 Result evaluate(uint32_t now)const{
  Result r;if(!samples)return r;float low[3],high[3];memcpy(low,points[0],sizeof(low));memcpy(high,points[0],sizeof(high));
  for(unsigned n=1;n<samples;n++)for(unsigned i=0;i<3;i++){low[i]=fminf(low[i],points[n][i]);high[i]=fmaxf(high[i],points[n][i]);}
  float radius[3],target=0;bool spanOk=true;
  for(unsigned i=0;i<3;i++){r.span[i]=high[i]-low[i];radius[i]=r.span[i]/2;target+=radius[i]/3;r.coefficients.offset[i]=(high[i]+low[i])/2;if(r.span[i]<30||r.span[i]>200)spanOk=false;}
  if(!spanOk)return r;
  for(unsigned i=0;i<3;i++)r.coefficients.scale[i]=target/radius[i];r.coefficients.enabled=true;
  double sum=0,square=0;
  for(unsigned n=0;n<samples;n++){
   float v[3];apply(r.coefficients,points[n],v);unsigned oct=0;for(unsigned i=0;i<3;i++)if(v[i]>=0)oct|=1u<<i;
   ++r.octants[oct];double magnitude=sqrt(double(v[0])*v[0]+double(v[1])*v[1]+double(v[2])*v[2]);sum+=magnitude;square+=magnitude*magnitude;
  }
  r.meanRadius=sum/samples;r.rms=r.meanRadius>0?sqrt(fmax(0.,square/samples-double(r.meanRadius)*r.meanRadius))/r.meanRadius:1;
  r.fitEvaluated=true;r.radiusMatch=r.meanRadius>=target*0.85f&&r.meanRadius<=target*1.15f;
  bool coverage=true;for(auto n:r.octants)if(n<5)coverage=false;
  r.ready=active&&samples>=minSamples&&!saturated&&uint32_t(now-startedMs)>=20000&&uint32_t(now-startedMs)<180000&&uint32_t(now-lastMs)<500&&coverage&&r.rms<=0.15f&&r.radiusMatch&&valid(r.coefficients);
  return r;
 }
 bool timedOut(uint32_t now)const{return active&&uint32_t(now-startedMs)>=180000;}
};
#pragma pack(push,1)
struct Record {uint32_t magic=0x4c41434d;uint16_t version=1,bytes=sizeof(Record);uint32_t sequence=0;uint8_t enabled=0,reserved[3]{};float offset[3]{},scale[3]{1,1,1};uint32_t crc=0;};
#pragma pack(pop)
inline uint32_t crc32(const void* data,unsigned bytes){const uint8_t* p=(const uint8_t*)data;uint32_t c=~0u;while(bytes--){c^=*p++;for(unsigned i=0;i<8;i++)c=(c>>1)^(0xedb88320u&(0u-(c&1)));}return ~c;}
inline Record record(const Coefficients& c,uint32_t seq){Record r;r.sequence=seq;r.enabled=c.enabled;memcpy(r.offset,c.offset,sizeof(r.offset));memcpy(r.scale,c.scale,sizeof(r.scale));r.crc=crc32(&r,sizeof(r)-4);return r;}
inline bool decode(const Record& r,Coefficients& c){if(r.magic!=0x4c41434d||r.version!=1||r.bytes!=sizeof(r)||r.enabled>1||r.crc!=crc32(&r,sizeof(r)-4))return false;memcpy(c.offset,r.offset,sizeof(c.offset));memcpy(c.scale,r.scale,sizeof(c.scale));c.enabled=r.enabled;return valid(c);}
}
