#include "binary_log.h"
#include "acquisition.h"
#include <cassert>
#include <cstdio>
#include <iostream>
template<class T> void record(FILE* f,uint16_t kind,const T& payload){aqbin::RecordHeader h;h.kind=kind;h.bytes=sizeof(payload);h.monoUs=0x100000010ull;h.sequence=42;aqbin::seal(h,&payload);assert(aqbin::valid(h,&payload));auto bad=h;bad.sequence++;assert(!aqbin::valid(bad,&payload));assert(fwrite(&h,1,sizeof(h),f)==sizeof(h));assert(fwrite(&payload,1,sizeof(payload),f)==sizeof(payload));}
int main(int argc,char** argv){
 assert(argc==2);aq::AcquisitionConfig c;
 for(auto hz:aq::frequencies){c.frequency=hz;assert(aq::validConfig(c));assert(hz*4<=800000);}
 c.frequency=20001;assert(!aq::validConfig(c));c.frequency=200000;
 c.seconds=0;assert(!aq::validConfig(c));c.seconds=6;assert(!aq::validConfig(c));c.seconds=3;
 c.every=0;assert(!aq::observeShot(0,c));c.every=30;assert(aq::observeShot(0,c)&&aq::observeShot(30,c)&&!aq::observeShot(29,c));c.every=301;assert(!aq::validConfig(c));c.every=30;
 assert(!aq::observeShot(0,c,false));assert(aq::observeShot(0,c,true));
 assert(aq::observationHoldUs(c)==3500000);assert(aq::firstEnvelopeIndex(0x100000000ull,0x100000033ull)==2);assert(aq::firstEnvelopeIndex(100,99)==0);
 auto plan=aq::envelopeRead(1,0,60000,16384,1024);assert(plan.index==1 && !plan.count && !plan.lost);
 plan=aq::envelopeRead(0,1023,60000,16384,1024);assert(!plan.count);
 plan=aq::envelopeRead(0,20000,60000,16384,1024);assert(plan.index==3616 && plan.lost==3616 && plan.count==1024);
 plan=aq::envelopeRead(59000,60000,60000,16384,1024);assert(plan.count==1000);
 plan=aq::envelopeRead(59000,100000,60000,16384,1024);assert(plan.index==60000 && plan.lost==1000 && !plan.count);
 aq::Packet old;old.kind=aq::RESULT;old.version=2;aq::seal(old);assert(!aq::valid(old));
 aqbin::FileHeader file;file.role=2;file.stream=1;file.session=123;file.createdUs=0x100000001ull;aqbin::seal(file);assert(aqbin::valid(file));auto bad=file;bad.role=1;assert(!aqbin::valid(bad));
 FILE* f=fopen(argv[1],"wb");assert(f);assert(fwrite(&file,1,sizeof(file),f)==sizeof(file));
 aqbin::Imu imu{};imu.index=42;imu.fresh=7;imu.acquired=0x100000010ull;imu.utcSource=1;imu.values[0]=1.25;imu.values[12]=-9.5;record(f,aqbin::IMU,imu);
 aqbin::Bno bno{};bno.sensor=8;bno.sequence=255;bno.values[3]=-0.25;record(f,aqbin::BNO,bno);
 aqbin::Health h{};h.sensor.flags=8;h.sensor.observationSamples=60000;h.sensor.volts=11.5;h.sensor.observationActive=1;h.offset=-1234;h.frequency=50000;h.observationSeconds=3;h.observationEvery=30;strcpy(h.rtc,"2026-10-09T12:00:00");record(f,aqbin::HEALTH,h);
 aqbin::TimePosition pos{};pos.latitude=351234567;pos.longitude=1391234567;pos.positionValid=1;pos.accuracyMm=1250;pos.phoneAgeMs=-1;record(f,aqbin::TIME_POSITION,pos);
 aqbin::Shot shot{};shot.rate=4;shot.pattern=1;shot.patternId=17;shot.patternGap1=1600;shot.patternGap2=8200;shot.frequency=10000;shot.tof=20;shot.state=2;record(f,aqbin::SHOT,shot);
 aqbin::CaptureBegin begin{};begin.shot=42;begin.sampleRate=20000;begin.seconds=3;begin.targetSamples=60000;record(f,aqbin::CAPTURE_BEGIN,begin);
 struct Envelope {aqbin::EnvelopePrefix prefix;uint16_t values[3];} __attribute__((packed));Envelope block{{42,7,3,50},{0,4095,0x8001}};record(f,aqbin::ENVELOPE,block);
 aqbin::Edge edge{42,1};record(f,aqbin::COMPARATOR,edge);
 aqbin::CaptureEnd end{42,60000,0,0,0,aqbin::COMPLETE};record(f,aqbin::CAPTURE_END,end);assert(!fclose(f));
 std::cout<<"PASS acquisition bounds/cadence, local DMA indices, protocol v2 rejection, binary CRC/header and cross-language fixture; sizes imu="<<sizeof(imu)<<" bno="<<sizeof(bno)<<" health="<<sizeof(h)<<" packet="<<sizeof(aq::Packet)<<"\n";
}
