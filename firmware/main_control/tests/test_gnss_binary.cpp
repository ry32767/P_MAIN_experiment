#include "gnss_binary.h"
#include <assert.h>
#include <stdio.h>
#include <vector>
int main(){
 static gnssbin::UbxParser p;uint8_t b[100],payload[]={1,2,3,0xb5,0x62};size_t n=gnssbin::encode(2,0x15,payload,5,b);bool got=false;
 for(size_t i=0;i<n;i++)got=p.push(b[i])||got;assert(got&&p.bytes==n&&!memcmp(p.frame+6,payload,5));
 b[n-1]^=1;for(size_t i=0;i<n;i++)assert(!p.push(b[i]));assert(p.errors==1);
 uint8_t huge[]={0xb5,0x62,2,0x15,0xff,0xff};for(auto c:huge)p.push(c);assert(p.errors==2&&!p.active());
 n=gnssbin::encode(0x0a,4,nullptr,0,b);for(size_t i=0;i<n;i++)got=p.push(b[i]);assert(got);
 static uint8_t full[gnssbin::maxUbx],data[gnssbin::maxUbx-8];memset(data,0x62,sizeof(data));n=gnssbin::encode(2,0x15,data,sizeof(data),full);for(size_t i=0;i<n;i++)got=p.push(full[i]);assert(got&&p.bytes==sizeof(full));
 gnssbin::RtcmParser r;uint8_t f[]={0xd3,0,3,0x43,0x20,0,0,0,0};auto crc=gnssbin::crc24(f,6);f[6]=crc>>16;f[7]=crc>>8;f[8]=crc;
 for(auto c:f)got=r.push(c);assert(got&&r.bytes==9);f[8]^=1;for(auto c:f)assert(!r.push(c));assert(r.errors==1);r.push(0xd3);r.push(0xfc);assert(r.errors==2);
 const uint8_t text[]="123456789";assert(gnssbin::crc24(text,9)==0xcde703);
 gnssbin::Station s;char line[]="STR;BASE_A;Base;RTCM 3.2;1074(1),1084(1);2;GPS+GLO;net;JPN;35.0;139.0;1;0;rx;none;B;N;9600;";
 assert(gnssbin::station(line,s)&&s.gga&&!strcmp(s.mount,"BASE_A"));assert(gnssbin::distance(35,139,s)==0);assert(gnssbin::distance(35.01,139,s)>1100&&gnssbin::distance(35.01,139,s)<1120);
 assert(!strcmp(s.name,"Base")&&!strcmp(s.format,"RTCM 3.2")&&!strcmp(s.details,"1074(1),1084(1)")&&!strcmp(s.systems,"GPS+GLO")&&!strcmp(s.network,"net"));
 char bounded[4];gnssbin::stationText(bounded,"abcdef");assert(!strcmp(bounded,"abc"));
 // GPS MSM4: 1 satellite, IDs 2 (L1C) and 23 (L5Q), active cells.
 uint8_t msm[31]{};msm[0]=0xd3;msm[2]=25;
 auto put=[&](unsigned pos,unsigned n,uint32_t v){for(unsigned i=0;i<n;++i){unsigned bit=pos+i;msm[bit/8]|=((v>>(n-i-1))&1)<<(7-bit%8);}};
 put(24,12,1074);put(97,1,1);put(162,1,1);put(183,1,1);put(193,2,3);
 bool l1=false,l5=false;assert(gnssbin::gpsMsmSignals(msm,sizeof(msm),l1,l5)&&l1&&l5);
 msm[194/8]&=~(1<<(7-194%8));assert(gnssbin::gpsMsmSignals(msm,sizeof(msm),l1,l5)&&l1&&!l5); // Mask alone is insufficient.
 assert(!gnssbin::gpsMsmSignals(msm,27,l1,l5));
 msm[3]=0x44;assert(!gnssbin::gpsMsmSignals(msm,sizeof(msm),l1,l5)); // Other constellation.
 char bad[]="STR;../../bad;Base;RTCM 3.2;x;2;GPS;net;JPN;35;139;0;0;rx;none;B;N;9600;";assert(!gnssbin::station(bad,s));
 char nan[]="STR;BASE;Base;RTCM 3.2;x;2;GPS;net;JPN;nan;139;0;0;rx;none;B;N;9600;";assert(!gnssbin::station(nan,s));
 char single[]="STR;BASE;Base;RTCM 3.2;x;1;GPS;net;JPN;35;139;0;0;rx;none;B;N;9600;";assert(!gnssbin::station(single,s));
 assert(!gnssbin::mountValid("X\r\nAuthorization"));puts("PASS UBX framing/checksum/bounds, RTCM CRC24/resync, source-table validation and nearest distance");
}
