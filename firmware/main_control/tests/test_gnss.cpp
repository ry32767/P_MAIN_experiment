#include "gnss_nmea.h"
#include <assert.h>
#include <stdio.h>
#include <string>
bool feed(gnss::Parser& p,const char* body,spnav::Nav& n,bool corrupt=false){
 unsigned c=0;for(auto q=body;*q;++q)c^=*q;char tail[8];snprintf(tail,sizeof(tail),"*%02X\r\n",corrupt?c^1:c);
 std::string s="$"+std::string(body)+tail;bool got=false;for(char ch:s)got=p.push(ch,n)||got;return got;
}
int main(){
 gnss::Parser p;spnav::Nav n;
 assert(feed(p,"GNRMC,120000.00,A,,,,,,,101026,,,A",n));assert(n.flags&spnav::time_valid);
 auto epoch=n.utc_s;
 assert(!feed(p,"GNRMC,120000.00,A,,,,,,,101026,,,A",n,true));assert(p.errors==1);
 assert(feed(p,"GNRMC,120000.00,V,,,,,,,101026,,,N",n));assert(!n.flags);
 assert(feed(p,"GNRMC,120000.10,A,,,,,,,101026,,,A",n));assert(!n.flags);
 assert(feed(p,"GNRMC,120000.00,A,,,,,,,311126,,,A",n));assert(!n.flags);
 for(int i=0;i<300;++i)p.push('x',n);
 assert(feed(p,"GPRMC,120000,A,,,,,,,101026,,,A",n));
 gnss::Clock clock;
 for(int i=0;i<3;++i){clock.pps(1000000ULL*(i+1));n.utc_s=epoch+i;n.flags=spnav::time_valid;clock.accept(n,1000000ULL*(i+1)+100000);}
 assert(clock.locked(3100000));assert(clock.utc(3100000)==(epoch+2)*1000000+100000);
 assert(!clock.locked(4500000));
 clock.pps(4000000);n.flags=0;clock.accept(n,4100000);assert(!clock.locked(4100000));
 gnss::Clock bad;
 bad.pps(1000000);n.flags=1;n.utc_s=epoch;bad.accept(n,1900000);assert(!bad.locked(1900000));assert(bad.rejected==1);
 puts("GNSS NMEA/PPS tests passed");
}
