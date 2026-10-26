#include "mag_calibration.h"
#include <assert.h>
#include <stdio.h>
static magcal::Calibration cal;
static void sphere(magcal::Calibration& c,uint32_t start,unsigned count=600){
 for(unsigned i=0;i<count;i++){
  double z=1.-2.*(i+.5)/count,phi=i*2.39996322972865332,r=sqrt(1-z*z);
  float raw[]={float(20+60*r*cos(phi)),float(-10+40*r*sin(phi)),float(5+50*z)};
  c.add(raw,false,start+i*100);
 }
}
int main(){
 cal.start(1000);sphere(cal,1000);auto result=cal.evaluate(61000);
 assert(result.ready&&result.rms<.02&&result.fitEvaluated&&result.radiusMatch);
 assert(fabs(result.coefficients.offset[0]-20)<.5&&fabs(result.coefficients.offset[1]+10)<.5&&fabs(result.coefficients.offset[2]-5)<.5);
 float raw[]={80,-10,5},out[3];magcal::apply(result.coefficients,raw,out);assert(fabs(out[0]-50)<1&&fabs(out[1])<1&&fabs(out[2])<1);
 assert(!cal.evaluate(61500).ready);cal.end();assert(!cal.evaluate(61000).ready);
 cal.start(1000);for(unsigned i=0;i<600;i++){float stationary[]={20,-10,5};cal.add(stationary,false,1000+i*100);}assert(!cal.evaluate(61000).ready);
 cal.start(1000);for(unsigned i=0;i<600;i++){float planar[]={float(50*cos(i*.1)),float(50*sin(i*.1)),5};cal.add(planar,false,1000+i*100);}assert(!cal.evaluate(61000).ready);assert(!cal.evaluate(61000).fitEvaluated);
 cal.start(1000);sphere(cal,1000);float clipped[]={0,0,479};cal.add(clipped,true,61000);assert(cal.saturated==1&&!cal.evaluate(61000).ready);
 cal.start(1000);float invalid[]={NAN,0,0};cal.add(invalid,false,1000);assert(cal.samples==0&&cal.rejected==1);
 cal.start(0xffffff00u);sphere(cal,0xffffff00u);assert(cal.evaluate(uint32_t(0xffffff00u+60000)).ready);assert(cal.timedOut(uint32_t(0xffffff00u+180000)));assert(!cal.evaluate(uint32_t(0xffffff00u+180000)).ready);
 cal.start(1000);sphere(cal,1000,100);assert(!cal.evaluate(11000).ready);
 auto record=magcal::record(result.coefficients,42);magcal::Coefficients decoded;assert(magcal::decode(record,decoded)&&decoded.enabled);record.offset[1]+=1;assert(!magcal::decode(record,decoded));
 record=magcal::record(result.coefficients,42);record.version=2;record.crc=magcal::crc32(&record,sizeof(record)-4);assert(!magcal::decode(record,decoded));
 magcal::Coefficients identity;record=magcal::record(identity,43);assert(magcal::decode(record,decoded)&&!decoded.enabled);magcal::apply(decoded,raw,out);assert(out[0]==raw[0]);
 puts("PASS magnetic offset/scale fit, stationary/planar/incomplete/saturated/stale rejection, millis wrap, CRC/version and reset records");
}
