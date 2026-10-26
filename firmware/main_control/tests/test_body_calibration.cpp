#include "body_calibration.h"
#include "spresense_control_protocol.h"
#include <cassert>
#include <cstdio>
int main(){
 bodycal::Capture c;assert(!c.mark(1,false,1));assert(c.begin(4,1,2,0x100000000ULL));assert(!c.begin(5,1,2,1));
 uint64_t t=0x100000100ULL;
 for(unsigned p=1;p<=5;p++){
  assert(c.mark(p,false,t));assert(!c.mark(p+1,false,t));assert(!c.mark(p,true,t+4999999));
  assert(c.mark(p,true,t+5000000));assert(!c.mark(p,false,t+5000001));t+=6000000;
 }
 assert(c.complete()&&c.mask==31);c.active=false;assert(!c.complete());
 c={};assert(c.begin(1,1,1,0));assert(!c.mark(1,false,bodycal::maximumSessionUs+1));
 c={};assert(c.begin(2,1,1,0,16));assert(!c.mark(1,false,1));assert(c.mark(5,false,1)&&c.mark(5,true,30000001)&&c.complete()&&c.mask==16);
 bodycal::Motion motion;motion.begin(100);motion.observe(7,3000100,1);motion.observe(0,30000100,2);assert(!motion.ready(30000100));motion.observe(1,31000100,3);assert(motion.ready(31000100));assert(!motion.ready(37000100));
 spnav::Nav nav;assert(!sprctl::motionAvailable(nav));nav.satellites=0xa3;sprctl::encodeMotion(nav,5);assert(sprctl::motionAvailable(nav)&&(nav.fix&7)==5);nav.flags=spnav::position_valid;assert(!sprctl::motionAvailable(nav));
 bodycal::Profile p;p.generation=1;bodycal::seal(p);assert(bodycal::valid(p));assert(!bodycal::matches(p,0,0));
 p.imu=3;p.mag=4;p.axes=3;bodycal::seal(p);assert(bodycal::valid(p)&&bodycal::matches(p,3,4)&&!bodycal::matches(p,4,4));
 p.imuQ[0]=2;bodycal::seal(p);assert(!bodycal::valid(p));p.imuQ[0]=1;p.cameraScale=1.1;bodycal::seal(p);assert(!bodycal::valid(p));
 p.cameraScale=1;p.cameraOffsetUs=1792886400000000.;bodycal::seal(p);assert(bodycal::valid(p));p.cameraOffsetUs=NAN;bodycal::seal(p);assert(!bodycal::valid(p));p.cameraOffsetUs=0;bodycal::seal(p);p.crc^=1;assert(!bodycal::valid(p));
 puts("PASS body recording phases, minimum time, duplicates, 64-bit clock, expiry, quaternion, references and CRC");
}
