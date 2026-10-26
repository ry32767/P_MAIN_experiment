#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
namespace gnssbin {
constexpr size_t maxUbx=8192,maxRtcm=1029;
inline uint16_t u16(const uint8_t* p){return p[0]|uint16_t(p[1])<<8;}
inline uint32_t u32(const uint8_t* p){return p[0]|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
inline uint32_t crc24(const uint8_t* p,size_t n){uint32_t c=0;while(n--){c^=uint32_t(*p++)<<16;for(unsigned i=0;i<8;i++)c=(c<<1)^((c&0x800000)?0x1864cfb:0);}return c&0xffffff;}
class UbxParser {
 size_t used=0,target=0;
public:
 uint8_t frame[maxUbx]{};uint32_t errors=0;size_t bytes=0;
 void reset(){used=target=0;}
 bool active()const{return used!=0;}
 bool push(uint8_t c){
  if(!used){if(c==0xb5)frame[used++]=c;return false;}
  if(used==1&&c!=0x62){reset();if(c==0xb5)frame[used++]=c;return false;}
  frame[used++]=c;
  if(used==6){target=8+u16(frame+4);if(target>maxUbx){++errors;reset();return false;}}
  if(!target||used<target)return false;
  uint8_t a=0,b=0;for(size_t i=2;i<target-2;i++){a+=frame[i];b+=a;}
  bool ok=a==frame[target-2]&&b==frame[target-1];bytes=target;reset();if(!ok)++errors;return ok;
 }
};
class RtcmParser {
 size_t used=0,target=0;
public:
 uint8_t frame[maxRtcm]{};size_t bytes=0;uint32_t errors=0;
 void reset(){used=target=0;}
 bool push(uint8_t c){
  if(!used){if(c==0xd3)frame[used++]=c;return false;}
  if(used==1&&(c&0xfc)){++errors;reset();return false;}
  frame[used++]=c;if(used==3)target=6+((frame[1]&3)<<8)+frame[2];
  if(!target||used<target)return false;
  uint32_t got=uint32_t(frame[target-3])<<16|uint32_t(frame[target-2])<<8|frame[target-1];
  bool ok=got==crc24(frame,target-3);bytes=target;reset();if(!ok)++errors;return ok;
 }
};
inline size_t encode(uint8_t cls,uint8_t id,const uint8_t* p,uint16_t n,uint8_t* out){
 out[0]=0xb5;out[1]=0x62;out[2]=cls;out[3]=id;out[4]=n;out[5]=n>>8;if(n)memcpy(out+6,p,n);
 uint8_t a=0,b=0;for(size_t i=2;i<size_t(n)+6;i++){a+=out[i];b+=a;}out[n+6]=a;out[n+7]=b;return n+8;
}
struct Station {char mount[64]{},name[64]{},format[24]{},details[160]{},systems[48]{},network[48]{};double lat=0,lon=0;bool gga=false;};
template<size_t N> inline void stationText(char (&out)[N],const char* in){size_t i=0;for(;i<N-1&&in[i];++i)out[i]=uint8_t(in[i])<32?' ':in[i];out[i]=0;}
inline bool coordinate(const char* s,double& out,double bound){char* end=nullptr;if(!*s)return false;out=strtod(s,&end);return end&&!*end&&isfinite(out)&&fabs(out)<=bound;}
inline bool mountValid(const char* s){size_t n=strlen(s);if(!n||n>=64)return false;for(size_t i=0;i<n;i++)if(!((s[i]>='A'&&s[i]<='Z')||(s[i]>='a'&&s[i]<='z')||(s[i]>='0'&&s[i]<='9')||s[i]=='_'||s[i]=='-'||s[i]=='.'))return false;return true;}
inline bool station(char* line,Station& s){
 char* f[20];unsigned n=1;f[0]=line;for(char* p=line;*p&&n<20;p++)if(*p==';'){*p=0;f[n++]=p+1;}
 if(n<18||strcmp(f[0],"STR")||!mountValid(f[1])||strncmp(f[3],"RTCM 3",6)||strcmp(f[5],"2")||strcmp(f[14],"none")||(strcmp(f[15],"N")&&strcmp(f[15],"B"))||!coordinate(f[9],s.lat,90)||!coordinate(f[10],s.lon,180))return false;
 strcpy(s.mount,f[1]);stationText(s.name,f[2]);stationText(s.format,f[3]);stationText(s.details,f[4]);stationText(s.systems,f[6]);stationText(s.network,f[7]);s.gga=!strcmp(f[11],"1");return true;
}
inline uint32_t bits(const uint8_t* p,unsigned pos,unsigned n){uint32_t v=0;while(n--){v=(v<<1)|((p[pos/8]>>(7-pos%8))&1);++pos;}return v;}
// CRC-validated GPS MSM signal masks only: presence is not phase usability.
// RTKLIB src/rtcm3.c msm_sig_gps: IDs 2..6,30..32=L1,22..24=L5.
inline bool gpsMsmSignals(const uint8_t* frame,size_t n,bool& l1,bool& l5){
 if(n<28||frame[0]!=0xd3||n!=size_t(6+((frame[1]&3)<<8)+frame[2]))return false;
 unsigned type=bits(frame,24,12);if(type<1074||type>1077)return false;
 unsigned nsat=0;for(unsigned i=0;i<64;++i)nsat+=bits(frame,97+i,1);
 unsigned nsig=0;uint8_t ids[32];for(unsigned i=0;i<32;++i)if(bits(frame,161+i,1))ids[nsig++]=i+1;
 if(!nsat||!nsig||193+nsat*nsig>(n-3)*8)return false;
 l1=l5=false;
 for(unsigned j=0;j<nsig;++j){bool cell=false;for(unsigned i=0;i<nsat;++i)cell|=bits(frame,193+i*nsig+j,1);if(!cell)continue;
  unsigned id=ids[j];l1|=(id>=2&&id<=6)||(id>=30&&id<=32);l5|=id>=22&&id<=24;
 }return true;
}
inline double distance(double lat,double lon,const Station& s){constexpr double r=0.017453292519943295;double a=sin((s.lat-lat)*r/2),b=sin((s.lon-lon)*r/2);double h=a*a+cos(lat*r)*cos(s.lat*r)*b*b;return 12742000*asin(sqrt(fmin(1.,fmax(0.,h))));}
}
