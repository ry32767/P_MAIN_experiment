#pragma once
#include "spresense_protocol.h"
#include <stdlib.h>
namespace gnss {
// Only checksummed RMC with active fix and a whole UTC second may label PPS.
class Parser {
 char line[160]{}; unsigned used=0;
 static int hex(char c){return c>='0'&&c<='9'?c-'0':c>='A'&&c<='F'?c-'A'+10:c>='a'&&c<='f'?c-'a'+10:-1;}
 static int pair(const char* p){return p[0]>='0'&&p[0]<='9'&&p[1]>='0'&&p[1]<='9'?(p[0]-'0')*10+p[1]-'0':-1;}
public:
 uint32_t packets=0,errors=0; uint8_t satellites=0,fix=0;
 void reset(){used=0;}
 bool push(char c,spnav::Nav& nav){
  if(c=='$'){used=0;line[used++]=c;return false;}
  if(!used)return false;
  if(c=='\r')return false;
  if(c!='\n'){if(used+1>=sizeof(line)){++errors;reset();}else line[used++]=c;return false;}
  line[used]=0;reset();char* star=strchr(line,'*');
  if(!star || strlen(star)!=3 || hex(star[1])<0 || hex(star[2])<0){++errors;return false;}
  unsigned sum=0;for(char* p=line+1;p<star;++p)sum^=uint8_t(*p);
  if(sum!=unsigned(hex(star[1])*16+hex(star[2]))){++errors;return false;}
  *star=0;char* fields[24];unsigned n=0;fields[n++]=line+1;
  for(char* p=line+1;*p && n<24;++p)if(*p==','){*p=0;fields[n++]=p+1;}
  if(strlen(fields[0])!=5)return false;
  ++packets;
  if(!strcmp(fields[0]+2,"GGA") && n>7){fix=uint8_t(atoi(fields[6]));satellites=uint8_t(atoi(fields[7]));return false;}
  if(strcmp(fields[0]+2,"RMC") || n<10)return false;
  nav={};nav.sequence=packets;nav.satellites=satellites;nav.fix=fix;
  const char* t=fields[1];const char* d=fields[9];
  if(strlen(t)<6 || strlen(d)!=6 || strcmp(fields[2],"A"))return true;
  if(t[6] && t[6]!='.')return true;
  for(unsigned i=7;t[6] && t[i];++i)if(t[i]!='0')return true;
  int yy=pair(d+4);if(yy<20)return true;
  nav.utc_s=spnav::unix_seconds(2000+yy,pair(d+2),pair(d),pair(t),pair(t+2),pair(t+4));
  if(nav.utc_s)nav.flags=spnav::time_valid|spnav::position_valid;
  return true;
 }
};
class Clock {
 uint64_t edge=0,previous=0,epoch=0,anchor=0,received=0;
 unsigned good=0;uint32_t id=0,lastId=0;
public:
 uint32_t rejected=0,periodUs=0,uartAgeUs=0;
 void invalidate(){good=0;}
 void pps(uint64_t t){previous=edge;edge=t;++id;periodUs=previous?uint32_t(edge-previous):0;if(previous&&(edge-previous<999000 || edge-previous>1001000))invalidate();}
 void accept(const spnav::Nav& n,uint64_t now){
  uint64_t age=now>=edge?now-edge:UINT64_MAX;uartAgeUs=uint32_t(age);
  if(!(n.flags&spnav::time_valid)||!edge||age<2000||age>800000){invalidate();++rejected;return;}
  if(good && id==lastId){++rejected;return;}
  good=good&&id==lastId+1&&n.utc_s==epoch+1?(good<3?good+1:3):1;
  lastId=id;epoch=n.utc_s;anchor=edge;received=now;
 }
 uint64_t labelledMono()const{return anchor;}
 uint64_t labelledUtc()const{return epoch*1000000ULL;}
 bool locked(uint64_t now)const{return good>=3&&now>=anchor&&now-anchor<1500000&&now>=received&&now-received<1500000;}
 uint64_t utc(uint64_t now)const{return locked(now)?epoch*1000000ULL+now-anchor:0;}
};
}
