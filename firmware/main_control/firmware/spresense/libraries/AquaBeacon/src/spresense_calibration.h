#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "spresense_protocol.h"
namespace sprcal {
// Application calibration of the driver's existing output. Never writes IMU flash.
// SI units: acceleration m/s^2, angular velocity rad/s.  Raw samples remain intact.
constexpr double earthRate=7.292115e-5,pi=3.141592653589793;
constexpr uint64_t settleUs=2000000,holdUs=10000000,sessionUs=1200000000;
enum State:uint8_t { NONE=0,WAITING=1,COLLECTING=2,MOVING=3,READY=4,SAVED=5,ERROR=6 };
enum Rejection:uint8_t { GAP=1,NONFINITE=2,ROTATING=4,GRAVITY=8,POSE=16,JUMP=32,COVERAGE=64,VARIANCE=128 };
struct Coefficients {
 uint32_t generation=0;bool enabled=false;
 float gravity=9.80665f,latitude=0;
 float accelBias[3]{},accelScale[3]{1,1,1},gyroBias[3]{};
};
inline bool config(float gravity,float latitude){return isfinite(gravity)&&gravity>=9.7f&&gravity<=9.9f&&isfinite(latitude)&&latitude>=-90&&latitude<=90;}
inline bool valid(const Coefficients& c){
 if(!config(c.gravity,c.latitude))return false;
 for(unsigned i=0;i<3;i++)if(!isfinite(c.accelBias[i])||fabs(c.accelBias[i])>.5||!isfinite(c.accelScale[i])||c.accelScale[i]<.95||c.accelScale[i]>1.05||!isfinite(c.gyroBias[i])||fabs(c.gyroBias[i])>.02)return false;
 return true;
}
inline void apply(const Coefficients& c,const float a[3],const float g[3],float ca[3],float cg[3]){
 for(unsigned i=0;i<3;i++){ca[i]=c.enabled?(a[i]-c.accelBias[i])*c.accelScale[i]:a[i];cg[i]=c.enabled?g[i]-c.gyroBias[i]:g[i];}
}
inline void putFloat(uint8_t* p,float f){uint32_t u;memcpy(&u,&f,4);spnav::put32(p,u);}
inline float getFloat(const uint8_t* p){uint32_t u=spnav::get32(p);float f;memcpy(&f,&u,4);return f;}
constexpr unsigned recordSize=64;
inline void encode(const Coefficients& c,uint8_t* b){
 memset(b,0,recordSize);memcpy(b,"SCAL",4);b[4]=1;b[5]=c.enabled;
 spnav::put32(b+8,c.generation);putFloat(b+12,c.gravity);putFloat(b+16,c.latitude);
 for(unsigned i=0;i<3;i++){putFloat(b+20+4*i,c.accelBias[i]);putFloat(b+32+4*i,c.accelScale[i]);putFloat(b+44+4*i,c.gyroBias[i]);}
 spnav::put32(b+60,spnav::crc32(b,60));
}
inline bool decode(const uint8_t* b,Coefficients& c){
 if(memcmp(b,"SCAL",4)||b[4]!=1||b[5]>1||b[6]||b[7]||spnav::get32(b+56)||spnav::get32(b+60)!=spnav::crc32(b,60))return false;
 Coefficients n;n.enabled=b[5];n.generation=spnav::get32(b+8);n.gravity=getFloat(b+12);n.latitude=getFloat(b+16);
 for(unsigned i=0;i<3;i++){n.accelBias[i]=getFloat(b+20+4*i);n.accelScale[i]=getFloat(b+32+4*i);n.gyroBias[i]=getFloat(b+44+4*i);}
 if(!n.generation||!valid(n))return false;c=n;return true;
}
class Session {
 double means[6][6]{},mean[6]{},m2[6]{},previous[6]{};
 uint32_t count=0;uint64_t started=0,stable=0,last=0;bool havePrevious=false;
 void resetWindow(){count=0;stable=0;memset(mean,0,sizeof(mean));memset(m2,0,sizeof(m2));}
public:
 State state=NONE;uint8_t mask=0,pose=255,progress=0,lastRejection=0;uint32_t resets=0;
 Coefficients candidate;
 bool active()const{return state==WAITING||state==COLLECTING||state==MOVING||state==READY;}
 bool begin(float gravity,float latitude,uint64_t now){
  if(active()||!config(gravity,latitude))return false;
  *this=Session();candidate.gravity=gravity;candidate.latitude=latitude;started=now;state=WAITING;return true;
 }
 bool select(uint8_t p,uint64_t now){
  if(!active()||state==READY||p>=6||(mask&(1u<<p)))return false;
  pose=p;state=COLLECTING;progress=0;lastRejection=0;last=now;havePrevious=false;resetWindow();return true;
 }
 void end(){state=NONE;pose=255;progress=0;resetWindow();}
 void expire(uint64_t now){if(active()&&now>=started&&now-started>sessionUs){state=ERROR;progress=0;}}
 void feed(const float a[3],const float g[3],uint64_t now,bool gap=false){
  expire(now);if((state!=COLLECTING&&state!=MOVING)||pose>=6)return;
  double x[6]={a[0],a[1],a[2],g[0],g[1],g[2]},norm=0;uint8_t rejected=0;
  if(gap||now<last||now-last>3000)rejected|=GAP;
  for(unsigned i=0;i<6;i++)if(!isfinite(x[i]))rejected|=NONFINITE;
  for(unsigned i=0;i<3;i++){norm+=x[i]*x[i];if(fabs(x[i+3])>.02)rejected|=ROTATING;}
  norm=sqrt(norm);unsigned axis=pose/2;double sign=pose%2?-1:1;
  if(norm<candidate.gravity*.95||norm>candidate.gravity*1.05)rejected|=GRAVITY;
  if(x[axis]*sign<norm*.995)rejected|=POSE;
  if(havePrevious)for(unsigned i=0;i<6;i++)if(fabs(x[i]-previous[i])>(i<3?.08:.0045))rejected|=JUMP;
  memcpy(previous,x,sizeof(x));havePrevious=true;last=now;
  if(rejected){lastRejection=rejected;++resets;resetWindow();progress=0;state=MOVING;return;}
  state=COLLECTING;if(!stable)stable=now;
  uint64_t elapsed=now-stable;if(elapsed<settleUs)return;
  ++count;for(unsigned i=0;i<6;i++){double delta=x[i]-mean[i];mean[i]+=delta/count;m2[i]+=delta*(x[i]-mean[i]);}
  progress=uint8_t((elapsed-settleUs)*100/holdUs>100?100:(elapsed-settleUs)*100/holdUs);
  if(elapsed<settleUs+holdUs)return;
  // 960 Hz: 95% coverage and low variance. These are application gates, not Sony's proprietary thresholds.
  if(count<9120){lastRejection=COVERAGE;++resets;resetWindow();progress=0;state=MOVING;return;}
  // 1 mdps/sqrt(Hz) with a conservative 480 Hz bandwidth is ~0.000383 rad/s
  // RMS. Do not require a per-sample variance below the board's typical noise.
  for(unsigned i=0;i<6;i++)if(sqrt(m2[i]/(count-1))>(i<3?.02:.001)){lastRejection=VARIANCE;++resets;resetWindow();progress=0;state=MOVING;return;}
  memcpy(means[pose],mean,sizeof(mean));mask|=uint8_t(1u<<pose);pose=255;progress=100;
  state=mask==63?(solve()?READY:ERROR):WAITING;resetWindow();
 }
 bool solve(){
  for(unsigned i=0;i<3;i++){
   double positive=means[2*i][i],negative=means[2*i+1][i];
   if(positive-negative<candidate.gravity*1.9)return false;
   candidate.accelBias[i]=float((positive+negative)/2);
   candidate.accelScale[i]=float(2*candidate.gravity/(positive-negative));
  }
  // In each static pose, dot(gyro, up) = dot(bias, up) + EarthRate*sin(latitude).
  // Fit all six measured gravity directions. This preserves Earth rotation without
  // guessing the undocumented USB-to-sensor horizontal axis or magnetic declination.
  double aug[3][4]{};double up[6][3]{};
  double vertical=earthRate*sin(candidate.latitude*pi/180);
  for(unsigned p=0;p<6;p++){
   double norm=0;for(unsigned i=0;i<3;i++){up[p][i]=(means[p][i]-candidate.accelBias[i])*candidate.accelScale[i];norm+=up[p][i]*up[p][i];}
   norm=sqrt(norm);if(fabs(norm-candidate.gravity)>.05*candidate.gravity)return false;
   double y=-vertical;for(unsigned i=0;i<3;i++){up[p][i]/=norm;y+=means[p][i+3]*up[p][i];}
   for(unsigned i=0;i<3;i++){for(unsigned j=0;j<3;j++)aug[i][j]+=up[p][i]*up[p][j];aug[i][3]+=up[p][i]*y;}
  }
  for(unsigned i=0;i<3;i++){
   if(fabs(aug[i][i])<.1)return false;double d=aug[i][i];for(unsigned j=i;j<4;j++)aug[i][j]/=d;
   for(unsigned k=0;k<3;k++)if(k!=i){d=aug[k][i];for(unsigned j=i;j<4;j++)aug[k][j]-=d*aug[i][j];}
  }
  for(unsigned i=0;i<3;i++)candidate.gyroBias[i]=float(aug[i][3]);
  for(unsigned p=0;p<6;p++){
   double residual=-vertical;for(unsigned i=0;i<3;i++)residual+=(means[p][i+3]-candidate.gyroBias[i])*up[p][i];
   if(fabs(residual)>.0003)return false;
  }
  candidate.enabled=true;return valid(candidate);
 }
};
}
