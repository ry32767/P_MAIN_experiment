#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <atomic>
namespace aq {
constexpr uint32_t shotPeriodUs=1000000, windowUs=22000, earlyUs=200;
constexpr float soundSpeed=1500.0f;
constexpr uint32_t imuPeriodUs=1000000/IMU_HZ;
static_assert(IMU_HZ==50 || IMU_HZ==100,"Supported IMU rates: 50/100 Hz");
inline uint32_t crc32(const void* p,size_t n) {
 uint32_t c=~0u; auto b=static_cast<const uint8_t*>(p);
 while(n--){c^=*b++;for(int k=0;k<8;k++)c=(c>>1)^(0xedb88320u & (0u-(c&1)));}return ~c;
}
enum Kind:uint8_t { PROBE=1, REPLY=2, SCHEDULE=3, RESULT=4, STOP=5, CONTROL=6, CONTROL_REPLY=7, UTC_ANCHOR=8 };
#pragma pack(push,1)
struct Health {
 uint32_t uptimeMs=0, rows=0, durableRows=0, sdErrors=0, dropped=0, imuSamples=0, imuGaps=0, resets=0;
 uint32_t imuAgeUs=0xffffffff, maxLatenessUs=0, rxEdges=0;
 uint16_t flags=0, envMv=0, buttonsMv=0;
 float volts=0, currentMa=0, temperature=0, pressure=0, magX=0, magY=0, magZ=0;
};
// Flags indicate current health, never prove physical sensor calibration.
enum Flags:uint16_t {POWER=1,SD=2,IMU=4,LEAK=8,INA=16,TEMP=32,PRESSURE=64,RTC=128,MAG=256,RX_HIGH=512,UTC_VALID=1024,RTC_SAVED=2048};
struct Packet {
 uint16_t magic=0xa751; uint8_t version=1, kind=0; uint32_t session=0,seq=0;
 uint64_t t1=0,t2=0,t3=0; int64_t offset=0;
 uint32_t uncertainty=0; Health health{}; uint32_t crc=0;
};
#pragma pack(pop)
constexpr uint32_t airUs=(sizeof(Packet)*10*1000000ull+115199)/115200;
inline void seal(Packet& p){p.crc=crc32(&p,sizeof(p)-4);}
inline bool valid(const Packet&p){return p.magic==0xa751 && p.version==1 && p.kind>=PROBE && p.kind<=UTC_ANCHOR && p.crc==crc32(&p,sizeof(p)-4);}
struct Parser {
 uint8_t bytes[sizeof(Packet)]{};size_t used=0;uint32_t errors=0;
 bool push(uint8_t b,Packet& p){
  if(!used && b!=0x51)return false;
  if(used==1 && b!=0xa7){used=b==0x51?1:0;return false;}
  bytes[used++]=b;if(used!=sizeof(Packet))return false;
  memcpy(&p,bytes,sizeof(p));used=0;if(valid(p))return true;++errors;return false;
 }
};
struct Sync {
 int64_t offset=0;uint32_t uncertainty=0;bool ok=false;
 void update(uint64_t t1,uint64_t t2,uint64_t t3,uint64_t t4){
  ok=false;if(t4<t1 || t3<t2)return;
  int64_t rtt=int64_t(t4-t1)-int64_t(t3-t2);
  if(rtt<int64_t(2*airUs)-100 || rtt>int64_t(2*airUs)+10000)return;
  offset=((int64_t(t2)-int64_t(t1))+(int64_t(t3)-int64_t(t4)))/2;
  uncertainty=uint32_t((rtt>2*airUs?rtt-2*airUs:0)/2)+100;ok=true;
 }
};
template<class T,size_t N> struct Ring {
 T a[N];std::atomic<uint32_t> head{0},tail{0};
 bool push(const T&v){auto h=head.load(std::memory_order_relaxed);if(h-tail.load(std::memory_order_acquire)>=N)return false;a[h%N]=v;head.store(h+1,std::memory_order_release);return true;}
 bool pop(T&v){auto t=tail.load(std::memory_order_relaxed);if(t==head.load(std::memory_order_acquire))return false;v=a[t%N];tail.store(t+1,std::memory_order_release);return true;}
};
struct Sample {
 uint64_t scheduledUs=0,acquiredUs=0,sourceUs=0;uint32_t index=0,fresh=0;
 uint64_t utcUs=0;uint8_t utcSource=0;
 float qw=0,qx=0,qy=0,qz=0,ax=0,ay=0,az=0,gx=0,gy=0,gz=0,mx=0,my=0,mz=0;
};
}
