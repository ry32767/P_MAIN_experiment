#include "utc_clock.h"
#include "protocol.h"
#include <cassert>
#include <cstdio>
using namespace utcclock;
int main(){
 constexpr uint64_t e=1789952400000000ULL;
 Anchor g{100,e,1000,GPS},p{100,e,2000,PHONE},r{100,e,3000,RTC};
 assert(select(g,p,r,999).source==GPS);assert(select(g,p,r,1000).source==PHONE);
 assert(select(g,p,r,2000).source==RTC);assert(!select(g,p,r,3000).valid(3000));
 assert(!g.valid(99));assert(g.at(110)==e+10);
 auto c=transfer(900000000,e,-899000000,GPS,1010000);
 assert(c.valid(1010000)&&c.at(1010000)==e+10000);assert(!c.valid(5000000));
 assert(!transfer(1,e,-2,GPS,0).valid(0));assert(!transfer(INT64_MAX,e,1,GPS,1).valid(1));
 assert(!transfer(100,e,0,Source(9),100).valid(100));assert(!transfer(100,0,0,GPS,100).valid(100));
 assert(!transfer(100,e,0,GPS,99).valid(99));
 for(int y=2020;y<=2099;y++)for(int m=1;m<=12;m++){
  auto sec=spnav::unix_seconds(y,m,1,23,59,59);Date d;assert(fromEpoch(sec,d));
  assert(d.year==y&&d.month==m&&d.day==1&&d.hour==23&&d.minute==59&&d.second==59);
 }
 Date d;assert(!fromEpoch(UINT64_MAX,d));assert(!fromEpoch(0,d));
 uint8_t b[]={0x59,0x59,0x23,1,0x29,0x02,0x24};uint64_t out;
 assert(decodeRtc(b,0,out));assert(!decodeRtc(b,0x80,out));b[6]=0x25;assert(!decodeRtc(b,0,out));
 b[6]=0x24;b[5]=0x82;assert(!decodeRtc(b,0,out));b[5]=2;b[0]=0x6a;assert(!decodeRtc(b,0,out));
 b[0]=0;b[2]=0x72;assert(decodeRtc(b,0,out));assert(out==spnav::unix_seconds(2024,2,29,12,59,0)*1000000ULL);
 aq::Packet packet;packet.kind=aq::UTC_ANCHOR;aq::seal(packet);assert(aq::valid(packet));assert(sizeof(packet)==130);
 spnav::Clock early;
 for(unsigned i=0;i<3;i++){early.pps(1000000ULL*(i+1));assert(early.accept([](unsigned j){spnav::Nav n;n.flags=spnav::time_valid;n.sequence=j;n.utc_s=1800000000+j;return n;}(i),1000000ULL*(i+1)+10000));}
 assert(early.locked(3010000));early.pps(3500000);assert(!early.locked(3500010));
 puts("PASS UTC priority/expiry, signed child mapping, overflow, RTC dates/OSF/BCD, fixed packet size, early GNSS frames and spurious PPS");
}
