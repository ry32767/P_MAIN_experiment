#include "phone_reference.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
int main(){
 int64_t v[5]{};
 assert(phone::numbers("42/1/1800000000000/50",v,4)&&v[2]==1800000000000LL);
 for(auto s:{"1//2","1/2/3/4/5"," 1/2/3/4","1/2/3/4x","1/2/3/99999999999999999999999","1/2/NaN/3"})assert(!phone::numbers(s,v,4));
 phone::Reference p;assert(!p.timeFresh(1));auto id=p.probe(4294967290ULL);
 assert(!p.setTime(id+1,1800000000000LL,50,4294967300ULL));
 assert(!p.setTime(id,1800000000000LL,1001,4294967300ULL));
 assert(p.setTime(id,1800000000000LL,50,4294967300ULL));
 assert(!p.setTime(id,1800000000000LL,50,4294967300ULL));
 assert(p.utc(4294967390ULL)==1800000000100ULL);
 assert(p.timeFresh(4295147289ULL));assert(!p.timeFresh(4295147290ULL));
 id=p.probe(500);assert(!p.setTime(id,1800000000000LL,5,5501));
 id=p.probe(6000);assert(!p.setTime(id,0,5,6001));
 assert(p.position(-900000000,1800000000,0,100,7000));assert(p.positionFresh(66899));assert(!p.positionFresh(66900));
 assert(!p.position(900000001,0,1,0,7000));assert(!p.position(0,-1800000001LL,1,0,7000));assert(!p.position(0,0,-1,0,7000));assert(!p.position(0,0,0,60000,7000));
 p.clear();assert(!p.positionFresh(7100)&&!p.timeFresh(7100));
 puts("PASS phone numeric validation, bounds, RTT, token replay, expiry, 64-bit uptime, position age and clear");
}
