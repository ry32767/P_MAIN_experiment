#include "spresense_calibration.h"
#include "ImuBatch.h"
#include "sd_sector_write.h"
#include <assert.h>
#include <stdio.h>
#include <string>
void calibrate(float latitude,bool noisy=false) {
 sprcal::Session s;uint64_t now=0x100000000ULL;float gravity=9.801f;
 const float ab[3]={.015f,-.012f,.02f},scale[3]={1.002f,.998f,1.001f},gb[3]={.001f,-.002f,.003f};
 assert(s.begin(gravity,latitude,now));assert(!s.begin(gravity,latitude,now));
 assert(!s.select(6,now));
 for(unsigned p=0;p<6;p++){
  assert(s.select(p,now));
  float a[3],g[3];for(unsigned i=0;i<3;i++){a[i]=ab[i];g[i]=gb[i];}
  unsigned axis=p/2;float sign=p%2?-1:1;
  a[axis]+=sign*gravity/scale[axis];g[axis]+=sign*sprcal::earthRate*sin(latitude*sprcal::pi/180);
  // Different horizontal Earth components in each pose cannot be treated as bias.
  g[(axis+1)%3]+=sprcal::earthRate*cos(latitude*sprcal::pi/180);
  for(unsigned i=0;i<11600;i++){
   float measured[3];for(unsigned j=0;j<3;j++)measured[j]=g[j]+(noisy?(i%2?.0004f:-.0004f):0);
   now+=1042;s.feed(a,measured,now);
  }
  assert(s.mask&(1<<p));assert(!s.select(p,now));
 }
 assert(s.state==sprcal::READY);assert(s.mask==63);
 for(unsigned i=0;i<3;i++){
  assert(fabs(s.candidate.accelBias[i]-ab[i])<1e-5);
  assert(fabs(s.candidate.accelScale[i]-scale[i])<1e-5);
  assert(fabs(s.candidate.gyroBias[i]-gb[i])<1e-7);
 }
 float a[3]={ab[0],ab[1],ab[2]+gravity/scale[2]},g[3]={gb[0],gb[1],gb[2]+float(sprcal::earthRate*sin(latitude*sprcal::pi/180))},ca[3],cg[3];
 sprcal::apply(s.candidate,a,g,ca,cg);assert(fabs(ca[2]-gravity)<1e-5);assert(fabs(cg[2]-sprcal::earthRate*sin(latitude*sprcal::pi/180))<1e-7);
 auto coefficient=s.candidate;coefficient.generation=71;
 uint8_t bytes[sprcal::recordSize];sprcal::encode(coefficient,bytes);sprcal::Coefficients restored;
 assert(sprcal::decode(bytes,restored));assert(restored.generation==71&&restored.enabled);
 bytes[28]^=1;assert(!sprcal::decode(bytes,restored));bytes[28]^=1;
 coefficient.enabled=false;coefficient.generation++;sprcal::encode(coefficient,bytes);assert(sprcal::decode(bytes,restored)&&!restored.enabled);
 sprcal::apply(restored,a,g,ca,cg);assert(!memcmp(a,ca,sizeof(a))&&!memcmp(g,cg,sizeof(g)));
}
void rejectionDiagnostics(){
 const auto one=[](float ax,float az,float gz,uint64_t step,bool gap){
  sprcal::Session s;assert(s.begin(9.8f,35,100));assert(s.select(4,100));
  float a[3]={ax,0,az},g[3]={0,0,gz};s.feed(a,g,100+step,gap);
  assert(s.state==sprcal::MOVING&&s.resets==1);return s.lastRejection;
 };
 assert(one(0,9.8f,0,1042,true)==sprcal::GAP);
 assert(one(0,9.8f,0,4000,false)==sprcal::GAP);
 assert(one(0,9.8f,.03f,1042,false)==sprcal::ROTATING);
 assert(one(0,5,0,1042,false)==sprcal::GRAVITY);
 assert(one(9.8f,0,0,1042,false)==sprcal::POSE);
 assert(one(0,NAN,0,1042,false)&sprcal::NONFINITE);
 sprcal::Session s;assert(s.begin(9.8f,35,100));assert(s.select(4,100));
 float a[3]={0,0,9.8f},g[3]{};s.feed(a,g,1142);a[0]=.1f;s.feed(a,g,2184);
 assert(s.lastRejection&sprcal::JUMP);
 s.end();assert(s.begin(9.8f,35,100));assert(s.select(4,100));
 a[0]=0;uint64_t now=100;
 for(unsigned i=0;i<5000;i++){now+=2500;s.feed(a,g,now);}
 assert(s.lastRejection==sprcal::COVERAGE&&s.resets>0);
 s.end();assert(s.begin(9.8f,35,100));assert(s.select(4,100));now=100;
 for(unsigned i=0;i<11600;i++){now+=1042;a[2]=9.8f+(i%2?.03f:-.03f);s.feed(a,g,now);}
 assert(s.lastRejection==sprcal::VARIANCE&&s.resets>0);
}
int main(int argc,char** argv){
 rejectionDiagnostics();
 {assert(aqsd::writeLimit(0,0)==0);assert(aqsd::writeLimit(511,6000)==1);
  assert(aqsd::writeLimit(512,6000)==512);assert(aqsd::writeLimit(0x1000001ffULL,2)==1);
  for(unsigned offset=0;offset<512;offset++)for(unsigned length: {1u,511u,512u,513u,6000u,12800u}){
   unsigned done=0;while(done<length){size_t n=aqsd::writeLimit(offset+done,length-done);
    assert(n && n<=512 && n<=length-done && (offset+done)%512+n<=512);done+=n;}
   assert(done==length);
   // A short positive write must preserve the same sector boundary on retry.
   done=0;while(done<length){size_t n=aqsd::writeLimit(offset+done,length-done);done+=n>7?7:n;}
   assert(done==length);
  }
 }

 {char out[100]{};int n=0;assert(aqimu::appendFixed(out,sizeof(out),n,-0.0f,8));assert(!strcmp(out,",-0.00000000"));
  n=0;assert(aqimu::appendFixed(out,sizeof(out),n,9.80665f,8));char expected[100];snprintf(expected,sizeof(expected),",%.8f",9.80665f);assert(!strcmp(out,expected));
  n=0;assert(!aqimu::appendFixed(out,3,n,1.0f,8));assert(!aqimu::appendFixed(out,sizeof(out),n,NAN,8));
  for(int i=-10000;i<10000;i++){float v=i/997.0f;n=0;assert(aqimu::appendFixed(out,sizeof(out),n,v,8));snprintf(expected,sizeof(expected),",%.8f",v);assert(!strcmp(out,expected));}
 }

 calibrate(35);calibrate(-35);calibrate(0);calibrate(90);
 calibrate(35,true); // Typical 960 Hz noise must not make the hold impossible.
 sprcal::Session s;assert(!s.begin(NAN,0,1));assert(!s.begin(9.8f,91,1));assert(s.begin(9.8f,35,1));assert(s.select(4,1));
 float a[3]={0,0,9.8f},g[3]={0,0,0};uint64_t now=1;
 for(unsigned i=0;i<3000;i++){now+=1042;s.feed(a,g,now);}
 assert(s.progress>0);s.feed(a,g,now+1042,true);assert(s.progress==0&&s.state==sprcal::MOVING);
 a[0]=1;for(unsigned i=0;i<13000;i++){now+=1042;s.feed(a,g,now);}assert(s.mask==0);
 s.expire(1+sprcal::sessionUs+1);assert(s.state==sprcal::ERROR);s.end();assert(!s.active());
 aqimu::Batch batch{};batch.version=aqimu::magic;batch.count=1;auto& row=batch.rows[0];row.seq=1;row.received_us=0x100000010ULL;row.data={1,23,1,2,3,4,5,6};
 for(unsigned i=0;i<6;i++)row.corrected[i]=10+i;row.calibration=71;
 assert(aqimu::format(batch));std::string line(batch.csv,batch.length);assert(line.find("4294967312")!=std::string::npos);
 auto comma=line.rfind(',');assert(comma!=std::string::npos);auto crc=strtoul(line.c_str()+comma+1,nullptr,16);assert(crc==spnav::crc32((const uint8_t*)line.data(),comma));
 unsigned columns=1;for(char c:line)columns+=c==',';assert(columns==27);
 assert(row.data.gx==1&&row.corrected[0]==10);batch.version=0x494d5531;assert(!aqimu::format(batch));
 if(argc==2){
  batch.version=aqimu::magic;row.calibration=0;for(unsigned i=0;i<6;i++)row.corrected[i]=i+1;
  row.sample_mono_us=row.received_us-200;row.sample_utc_us=1792886400000000ULL;row.time_quality=2;row.set_generation=9;row.parent_session=987;
  assert(aqimu::format(batch));FILE* f=fopen(argv[1],"wb");assert(f);fputs(aqimu::csvHeader,f);assert(fwrite(batch.csv,1,batch.length,f)==batch.length);assert(fclose(f)==0);
 }
 puts("PASS Spresense six-pose calibration: bias/scale, Earth rotation preservation, north/south latitude, motion/gaps, expiry, CRC save/clear, raw and corrected CSV");
}
