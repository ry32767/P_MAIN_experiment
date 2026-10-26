#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include "mag_calibration.h"
namespace bodycal {
constexpr uint32_t acknowledgeUncalibrated=0x43414c21;
constexpr uint64_t minimumPhaseUs=5000000,maximumSessionUs=900000000;
constexpr uint64_t minimumMotionUs=30000000;
struct Motion {
 uint8_t axes=0;uint64_t began=0,first=0,last=0,updated=0;uint32_t sequence=0;bool have=false;
 void begin(uint64_t now){*this={};began=now;}
 void observe(uint8_t bits,uint64_t now,uint32_t seq){if(now<began||now-began<3000000||seq==sequence)return;sequence=seq;updated=now;have=true;if(bits){axes|=bits&7;if(!first)first=now;last=now;}}
 bool ready(uint64_t now)const{return have&&axes&&first&&last-first>=1000000&&now>=began&&now-began>=minimumMotionUs&&now-updated<5000000;}
};
// FRD: +X forward, +Y right, +Z down. Quaternions rotate sensor -> body.
#pragma pack(push,1)
struct Profile {
 uint32_t magic=0x59444f42;uint16_t version=1,bytes=sizeof(Profile);
 uint32_t generation=0,capture=0,boot=0,imu=0,mag=0;
 uint64_t utc=0;uint8_t utcSource=0,axes=0,camera=0,reserved=0;
 char device[20]{};float imuQ[4]{1,0,0,0},magQ[4]{1,0,0,0};
 double cameraScale=1,cameraOffsetUs=0;uint32_t crc=0;
};
struct Event {
 uint32_t capture=0,boot=0,imu=0,mag=0;uint64_t mono=0,utc=0;
 uint32_t sequence=0,drops=0,uncertainty=UINT32_MAX;
 uint8_t event=0,phase=0,utcSource=0,mask=0;
 char device[20]{},log[64]{},raw[64]{},health[64]{};
};
// Control import: fitted results are tied to a completed recording on this SD.
struct Solution {
 uint32_t capture=0,boot=0,imu=0,mag=0;uint8_t axes=0,camera=0,reserved[2]{};
 float imuQ[4]{1,0,0,0},magQ[4]{1,0,0,0};double cameraScale=1,cameraOffsetUs=0;
};
#pragma pack(pop)
static_assert(sizeof(Profile)==112 && sizeof(Solution)==68 && sizeof(Event)==260,"Body calibration ABI");
static_assert(offsetof(Profile,device)==40&&offsetof(Profile,imuQ)==60&&offsetof(Profile,cameraScale)==92&&offsetof(Event,device)==48&&offsetof(Solution,imuQ)==20&&offsetof(Solution,cameraScale)==52,"Body field offsets");
inline bool quaternion(const float* q){double n=0;for(unsigned i=0;i<4;i++){if(!isfinite(q[i]))return false;n+=double(q[i])*q[i];}return fabs(n-1)<.001;}
inline bool values(uint8_t axes,uint8_t camera,const float* i,const float* m,double scale,double offset){
 return axes<=3&&camera<=1&&quaternion(i)&&quaternion(m)&&isfinite(scale)&&scale>=.999&&scale<=1.001&&isfinite(offset)&&fabs(offset)<=4e15;
}
inline bool valid(const Profile& p){return p.magic==0x59444f42&&p.version==1&&p.bytes==sizeof(p)&&p.generation&&p.utcSource<=3&&!p.reserved&&memchr(p.device,0,sizeof(p.device))&&values(p.axes,p.camera,p.imuQ,p.magQ,p.cameraScale,p.cameraOffsetUs)&&p.crc==magcal::crc32(&p,sizeof(p)-4);}
inline void seal(Profile& p){p.crc=magcal::crc32(&p,sizeof(p)-4);}
inline bool matches(const Profile& p,uint32_t imu,uint32_t mag){return imu&&mag&&p.imu==imu&&p.mag==mag;}
struct Capture {
 bool active=false;uint8_t phase=0,mask=0,required=31;uint64_t started=0,phaseStarted=0;uint32_t id=0,imu=0,mag=0;
 bool begin(uint32_t n,uint32_t i,uint32_t m,uint64_t now,uint8_t wanted=31){if(active||!n||(wanted!=31&&wanted!=16))return false;*this={};active=true;id=n;imu=i;mag=m;started=now;required=wanted;return true;}
 bool mark(uint8_t p,bool ending,uint64_t now){
  if(!active||p<1||p>5||!(required&(1u<<(p-1)))||now<started||now-started>maximumSessionUs)return false;
  if(!ending){if(phase||(mask&(1u<<(p-1))))return false;phase=p;phaseStarted=now;return true;}
  if(phase!=p||now<phaseStarted||now-phaseStarted<minimumPhaseUs)return false;
  mask|=1u<<(p-1);phase=0;return true;
 }
 bool complete()const{return active&&mask==required&&!phase;}
};
}
