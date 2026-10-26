#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include "mag_calibration.h"
namespace parentcal {
// Body convention is right-handed FRD: +X forward, +Y right, +Z down.
// q=(w,x,y,z) rotates sensor coordinates to body. Position: body origin -> sensor, metres.
struct Mount {uint32_t known=0;float position[3]{},q[4]{1,0,0,0};};
struct Record {
 uint32_t magic=0x54455350;uint16_t version=1,bytes=sizeof(Record);
 uint32_t generation=0,parentSession=0,imuCalibration=0,magCalibration=0;
 uint64_t createdUtc=0;uint32_t utcSource=0;
 char name[40]{},cad[40]{},body[40]{},sensor[3][40]{};
 Mount mounts[3]; // IMU, magnetometer, GNSS antenna phase centre.
 uint32_t crc=0;
};
static_assert(sizeof(Mount)==32 && sizeof(Record)==376 && offsetof(Record,crc)==372,"Parent profile ABI");
inline bool text(const char* s,unsigned size){
 bool ended=false;for(unsigned i=0;i<size;i++){unsigned char c=s[i];if(!c)ended=true;else if(ended||c<32||c==127)return false;}return ended;
}
inline bool mountValid(const Mount& m){
 if(m.known>1)return false;double norm=0;
 for(auto x:m.position)if(!isfinite(x)||fabs(x)>100)return false;
 for(auto x:m.q){if(!isfinite(x))return false;norm+=double(x)*x;}
 return fabs(norm-1)<.001;
}
inline bool valid(const Record& r){
 if(r.magic!=0x54455350 || r.version!=1 || r.bytes!=sizeof(r) || !r.generation || r.utcSource>3 ||
  !text(r.name,40)||!text(r.cad,40)||!text(r.body,40))return false;
 for(unsigned i=0;i<3;i++)if(!text(r.sensor[i],40)||!mountValid(r.mounts[i]))return false;
 return r.crc==magcal::crc32(&r,sizeof(r)-4);
}
inline void seal(Record& r){r.crc=magcal::crc32(&r,sizeof(r)-4);}
inline bool geometryComplete(const Record& r){
 if(!r.name[0]||!r.cad[0]||!r.body[0])return false;
 for(unsigned i=0;i<3;i++)if(!r.sensor[i][0]||!r.mounts[i].known)return false;return true;
}
inline void rotate(const Mount& m,const float in[3],float out[3]){
 float v[3]={in[0],in[1],in[2]};const auto& q=m.q;
 float t[3]={2*(q[2]*v[2]-q[3]*v[1]),2*(q[3]*v[0]-q[1]*v[2]),2*(q[1]*v[1]-q[2]*v[0])};
 out[0]=v[0]+q[0]*t[0]+q[2]*t[2]-q[3]*t[1];out[1]=v[1]+q[0]*t[1]+q[3]*t[0]-q[1]*t[2];out[2]=v[2]+q[0]*t[2]+q[1]*t[1]-q[2]*t[0];
}
}
