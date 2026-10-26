#include "sensor_time.h"
#include "parent_calibration.h"
#include "binary_log.h"
#include <assert.h>
#include <stdio.h>
#include <string>
#undef assert
#define assert(condition) do { if(!(condition)){fprintf(stderr,"Failed line %d: %s\n",__LINE__,#condition);return 1;} } while(0)
int main(int argc,char** argv){
 for(unsigned step: {15594u,20588u,20686u})assert(sensortime::sampleIntervalValid(step,960));
 for(unsigned step: {0u,1000u,39800u})assert(!sensortime::sampleIntervalValid(step,960));
 fputs("PPS test\n",stderr);
 const uint64_t epoch=1792886400000000ULL,base=5000000;
 sensortime::PpsClock clock;assert(!clock.locked(base));assert(!clock.pair(epoch,100000,base,1125400));
 unsigned captured=0;
 for(unsigned label: {1u,4u,7u,10u}){
  uint64_t edge=base+uint64_t(label)*1000050,rx=edge+1300000;
  while(base+uint64_t(captured+1)*1000050<=rx){++captured;clock.edge({base+uint64_t(captured)*1000050,captured});}
  assert(clock.pair(epoch+uint64_t(label)*1000000,174600,rx,1125400));
  assert(clock.locked(rx)==(label>=7));
  if(label>=7){assert(clock.utc(edge,rx)==epoch+uint64_t(label)*1000000);assert(clock.utc(edge-1000050,rx)==epoch+uint64_t(label-1)*1000000);}
 }
 assert(!clock.locked(clock.latest.mono+1500000));
 clock.edge({clock.latest.mono+100,clock.latest.count+1});assert(!clock.locked(clock.latest.mono));
 assert(!clock.pair(epoch+12000001,0,clock.latest.mono,1125400));
 sensortime::PpsClock repeated;repeated.edge({1000000,1});
 for(unsigned i=0;i<5;i++)assert(repeated.pair(epoch,100000,2225400,1125400));
 assert(!repeated.locked(2225400));
 fputs("IMU test\n",stderr);
 sensortime::ImuClock imu;uint64_t previous=0;unsigned valid=0;
 uint32_t initial=0xffff0000u;
 for(unsigned i=0;i<12000;i++){
  uint32_t raw=initial+i*20000u;
  uint64_t host=10000000+uint64_t(double(i)*20000*(1000000.0/19200000.0)*1.00005)+200+(i%19)*30;
  uint64_t sample=imu.feed(raw,host);
  if(sample){assert(sample<=host);assert(sample>previous);previous=sample;++valid;}
 }
 assert(valid>9000 && imu.ready && imu.extended>UINT32_MAX);
 // Quantised/bursty host reads must not force an endless two-second warmup.
 sensortime::ImuClock burst;unsigned burstValid=0;
 for(unsigned i=0;i<12000;i++){
  uint64_t host=20000000+uint64_t(double(i)*20000*(1000000.0/19200000.0))/2500*2500;
  burstValid+=burst.feed(initial+i*20000u,host)!=0;
 }
 assert(burst.ready&&burst.generation==0&&burstValid>3000);
 auto gen=imu.generation;assert(!imu.feed(initial+11999u*20000u,25000000));assert(imu.generation>gen&&!imu.ready);
 fputs("profile test\n",stderr);
 parentcal::Record c;c.generation=1;parentcal::seal(c);assert(parentcal::valid(c));assert(!parentcal::geometryComplete(c));
 strcpy(c.name,"test");strcpy(c.cad,"rev1");strcpy(c.body,"parent");
 for(unsigned i=0;i<3;i++){strcpy(c.sensor[i],"sensor");c.mounts[i].known=1;}
 parentcal::seal(c);assert(parentcal::valid(c)&&parentcal::geometryComplete(c));
 c.mounts[0].q[0]=0;parentcal::seal(c);assert(!parentcal::valid(c));c.mounts[0].q[0]=1;
 c.mounts[1].position[0]=NAN;parentcal::seal(c);assert(!parentcal::valid(c));c.mounts[1].position[0]=0;
 c.mounts[0].q[0]=float(sqrt(.5));c.mounts[0].q[3]=float(sqrt(.5));float x[3]={1,0,0},y[3];parentcal::rotate(c.mounts[0],x,y);
 assert(fabs(y[0])<1e-6&&fabs(y[1]-1)<1e-6&&fabs(y[2])<1e-6);
 parentcal::seal(c);assert(parentcal::valid(c));c.cad[0]^=1;assert(!parentcal::valid(c));
 static_assert(sizeof(parentcal::Record)==376,"Profile wire ABI");
 static_assert(sizeof(aqbin::MagObservation)==84,"Mag wire ABI");
 static_assert(sizeof(aqbin::GnssObservation)==64,"GNSS wire ABI");
 if(argc==2){
  FILE* f=fopen(argv[1],"wb");assert(f);aqbin::FileHeader h;h.role=1;h.session=987;aqbin::seal(h);assert(fwrite(&h,1,sizeof(h),f)==sizeof(h));
  auto write=[&](uint16_t kind,const void* p,uint16_t n){aqbin::RecordHeader rh;rh.kind=kind;rh.bytes=n;rh.monoUs=12345;rh.sequence=19;aqbin::seal(rh,p);fwrite(&rh,1,sizeof(rh),f);fwrite(p,1,n,f);};
  aqbin::MagObservation m{};m.sequence=19;m.begin=12300;m.end=12345;m.utc=epoch;m.raw[0]=12;m.corrected[1]=-34;m.body[2]=56;m.bodyValid=1;m.set=9;m.valid=0x8801;write(aqbin::MAG_OBSERVATION,&m,sizeof(m));
  c.cad[0]^=1;parentcal::seal(c);write(aqbin::CALIBRATION_SET,&c,sizeof(c));
  aqbin::GnssObservation g{};g.itow=123456;g.received=12345;g.latitude=351234567;g.vn=-123;g.set=9;write(aqbin::GNSS_OBSERVATION,&g,sizeof(g));
  assert(fclose(f)==0);
 }
 puts("PASS PPS association/loss/glitches/duplicate labels/drift, IMU counter wrap/affine reconstruction/reset, parent profile CRC/geometry/quaternion");
}
