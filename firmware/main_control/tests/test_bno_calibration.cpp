#include "bno_calibration.h"
#include "control.h"
#include <cassert>
#include <iostream>
int main(){
 aq::BnoCalibration c;uint64_t t=0x100000000ULL;
 c.start(t,5);assert(c.active && !c.saved && c.previousMask==5 && !c.ready(t));
 for(unsigned i=0;i<3;i++)c.event(i,3,t);c.tick(t);
 for(unsigned n=1;n<=50;n++){for(unsigned i=0;i<3;i++)c.event(i,2,t+n*100000);c.tick(t+n*100000);}
 assert(c.ready(t+5000000));assert(!c.ready(t+5500000));c.tick(t+5500000);assert(!c.goodSince);
 for(unsigned i=0;i<3;i++)c.event(i,3,t+6000000);c.event(1,1,t+6000000);c.tick(t+6000000);assert(!c.ready(t+6000000));
 c.saved=true;c.clear();assert(!c.active && !c.saved && !c.fresh(0,t));
 c.start(t,1);assert(!c.expired(t+599999999));assert(c.expired(t+600000000));
 c.start(t+600000001,7);assert(!c.saved && !c.goodSince && !c.fresh(2,t+600000001));
 aq::Packet p;aq::Control in;in.op=aq::CAL_SAVE;in.error=aq::CAL_NOT_READY;aq::pack(p,in);auto out=aq::unpack(p);assert(out.op==aq::CAL_SAVE && out.error==aq::CAL_NOT_READY);
 assert((aq::CALIBRATING & (3<<12))==0);
 std::cout<<"PASS calibration freshness, continuous readiness, low accuracy, session reset, 64-bit timeout and control packing\n";
}
