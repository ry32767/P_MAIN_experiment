#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
namespace phone {
inline bool numbers(const char* s,int64_t* values,unsigned count){
 for(unsigned i=0;i<count;++i){
  if(!(*s>='0'&&*s<='9') && !(*s=='-'&&s[1]>='0'&&s[1]<='9'))return false;
  errno=0;char* end=nullptr;values[i]=strtoll(s,&end,10);if(errno==ERANGE)return false;
  if(i+1==count)return *end==0;
  if(*end!='/')return false;s=end+1;
 }return false;
}
struct Reference {
 uint64_t probeMs=0,anchorMs=0,epochMs=0,positionUntil=0;
 uint32_t token=0,rttMs=0;int32_t latE7=0,lonE7=0;uint32_t accuracyMm=0;
 bool haveProbe=false,haveTime=false,havePosition=false;
 uint32_t probe(uint64_t now){probeMs=now;haveProbe=true;return ++token;}
 bool setTime(uint32_t id,int64_t epoch,int64_t rtt,uint64_t now){
  if(!haveProbe || id!=token || now<probeMs || now-probeMs>5000 || epoch<1577836800000LL || epoch>=4102444800000LL || rtt<0 || rtt>1000)return false;
  haveProbe=false;anchorMs=probeMs;epochMs=epoch;rttMs=uint32_t(rtt);haveTime=true;return true;
 }
 bool timeFresh(uint64_t now)const{return haveTime && now>=anchorMs && now-anchorMs<180000;}
 uint64_t utc(uint64_t now)const{return timeFresh(now)?epochMs+now-anchorMs:0;}
 bool position(int64_t lat,int64_t lon,int64_t accuracy,int64_t age,uint64_t now){
  if(lat<-900000000LL || lat>900000000LL || lon<-1800000000LL || lon>1800000000LL || accuracy<0 || accuracy>100000000 || age<0 || age>=60000)return false;
  latE7=int32_t(lat);lonE7=int32_t(lon);accuracyMm=uint32_t(accuracy);positionUntil=now+60000-uint64_t(age);havePosition=true;return true;
 }
 bool positionFresh(uint64_t now)const{return havePosition && now<positionUntil;}
 void clear(){haveProbe=haveTime=havePosition=false;}
};
}
