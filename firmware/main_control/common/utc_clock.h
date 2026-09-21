#pragma once
#include <stdint.h>
#include "spresense_protocol.h"
namespace utcclock {
enum Source:uint8_t { NONE=0, GPS=1, PHONE=2, RTC=3 };
inline const char* name(Source s){return s==GPS?"gps_pps_candidate":s==PHONE?"phone_clock":s==RTC?"rtc":"none";}
inline bool epochValid(uint64_t us){return us>=1577836800000000ULL && us<4102444800000000ULL;}
struct Anchor {
 uint64_t mono=0,epoch=0,until=0;Source source=NONE;
 bool valid(uint64_t now)const{return source>=GPS && source<=RTC && epochValid(epoch) && now>=mono && now<until;}
 uint64_t at(uint64_t now)const{return valid(now)?epoch+now-mono:0;}
};
inline Anchor select(const Anchor& gps,const Anchor& phone,const Anchor& rtc,uint64_t now){
 return gps.valid(now)?gps:phone.valid(now)?phone:rtc.valid(now)?rtc:Anchor{};
}
// offset = child monotonic - parent monotonic; never add UART receipt latency to UTC.
inline Anchor transfer(uint64_t parentMono,uint64_t epoch,int64_t offset,Source source,uint64_t now){
 if(parentMono>INT64_MAX || !epochValid(epoch) || source<GPS || source>RTC)return {};
 if(offset>0 && parentMono>uint64_t(INT64_MAX-offset))return {};
 int64_t m=int64_t(parentMono)+offset;if(m<0)return {};
 Anchor a{uint64_t(m),epoch,uint64_t(m)+4000000,source};return a.valid(now)?a:Anchor{};
}
struct Date {uint16_t year;uint8_t month,day,hour,minute,second;};
inline bool fromEpoch(uint64_t seconds,Date& d){
 if(seconds<1577836800ULL || seconds>=4102444800ULL)return false;
 uint64_t days=seconds/86400;uint32_t t=seconds%86400;unsigned y=1970;
 auto leap=[](unsigned yr){return yr%4==0 && (yr%100!=0 || yr%400==0);};
 while(days>=unsigned(365+leap(y)))days-=365+leap(y++);
 const unsigned lengths[]={31,28,31,30,31,30,31,31,30,31,30,31};unsigned m=0;
 while(days>=lengths[m]+(m==1&&leap(y))) {days-=lengths[m]+(m==1&&leap(y));++m;}
 d={uint16_t(y),uint8_t(m+1),uint8_t(days+1),uint8_t(t/3600),uint8_t(t/60%60),uint8_t(t%60)};return true;
}
inline bool decodeRtc(const uint8_t* t,uint8_t status,uint64_t& epoch){
 if((status&0x80) || (t[5]&0x80))return false;
 auto bcd=[](uint8_t n)->int{return (n&15)>9 || (n>>4)>9?-1:(n>>4)*10+(n&15);};
 int y=bcd(t[6]),m=bcd(t[5]&31),d=bcd(t[4]&63),h=bcd(t[2]&((t[2]&64)?31:63)),mi=bcd(t[1]&127),s=bcd(t[0]&127);
 if(y<0 || m<0 || d<0 || h<0 || mi<0 || s<0)return false;
 if(t[2]&64){if(h<1||h>12)return false;h=h%12+((t[2]&32)?12:0);}
 uint64_t sec=spnav::unix_seconds(2000+y,m,d,h,mi,s);if(!sec)return false;epoch=sec*1000000ULL;return true;
}
}
