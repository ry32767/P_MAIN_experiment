#pragma once
#include "gnss_binary.h"
#include <atomic>
namespace gnssdiag {
// UBX-23006991 R02: CFG-SIGNAL and CFG-NAVHPG; RAM readback only.
constexpr uint32_t keys[]={0x1031001f,0x10310001,0x10310004,0x10310021,0x10310007,0x10310009,0x20140011};
struct Settings {
 uint8_t values[7]{},known=0;uint64_t receivedUs=0;
 bool complete()const{return known==127;}
 bool read(const uint8_t* p,size_t n,uint64_t now){
  if(n<4||p[0]!=1||p[1]!=0)return false; // VALGET v1, RAM layer.
  Settings next=*this;bool found=false;
  for(size_t i=4;i<n;){
   if(n-i<4)return false;uint32_t k=gnssbin::u32(p+i);i+=4;unsigned type=k>>28;
   size_t size=type==1||type==2?1:type==3?2:type==4?4:type==5?8:0;
   if(!size||n-i<size)return false;
   for(unsigned j=0;j<7;j++)if(k==keys[j]){next.values[j]=p[i];next.known|=1u<<j;found=true;}
   i+=size;
  }
  if(found){next.receivedUs=now;*this=next;}return found;
 }
};
struct L5Enable {
 bool attempted=false,waitingAck=false,acknowledged=false,failed=false,verified=false;
 uint32_t sentMs=0,ackMs=0;
 bool needed(const Settings& s)const{return !attempted&&s.complete()&&s.values[0]==1&&s.values[1]==1&&s.values[2]==0;}
 void sent(uint32_t now){attempted=waitingAck=true;sentMs=now;}
 void ack(bool accepted,uint32_t now){if(!waitingAck)return;waitingAck=false;acknowledged=accepted;failed=!accepted;ackMs=now;}
 bool blocked(uint32_t now){
  if(waitingAck&&uint32_t(now-sentMs)>=2000){waitingAck=false;failed=true;}
  return waitingAck||(acknowledged&&uint32_t(now-ackMs)<700);
 }
 void readback(const Settings& s){if(attempted&&s.complete()&&s.values[2]==1){verified=true;}}
};
struct Signals {
 uint64_t receivedUs=0;uint32_t reports=0;uint16_t total=0,phaseUsed=0,phaseCorrected=0,gpsL5=0,galE1=0,galE5a=0,galDualPhase=0;
 bool read(const uint8_t* p,size_t n,uint64_t now){
  if(n<8||p[4]!=0||n!=8+size_t(p[5])*16)return false;
  Signals next;next.receivedUs=now;next.reports=reports+1;next.total=p[5];uint8_t gal[256]{};
  for(unsigned j=0;j<p[5];j++){
   const auto* v=p+8+j*16;uint16_t flags=gnssbin::u16(v+10);bool used=flags&(1u<<4);
   next.phaseUsed+=used;next.phaseCorrected+=bool(flags&(1u<<7));
   // Count tracked signals (quality >= code lock); a listed idle signal is not reception.
   if(v[7]>=4){next.gpsL5+=v[0]==0&&(v[2]==6||v[2]==7);next.galE1+=v[0]==2&&(v[2]==0||v[2]==1);next.galE5a+=v[0]==2&&(v[2]==3||v[2]==4);}
   if(v[0]==2&&used){if(v[2]==0||v[2]==1)gal[v[1]]|=1;if(v[2]==3||v[2]==4)gal[v[1]]|=2;}
  }
  for(auto v:gal)next.galDualPhase+=v==3;*this=next;return true;
 }
};
struct Corrections {
 uint64_t receivedUs=0;uint32_t reports=0,used=0,unused=0,unknown=0,crcFailed=0;uint16_t lastType=0,lastStation=0;
 bool read(const uint8_t* p,size_t n,uint64_t now){
  if(n!=8||p[0]!=2)return false;receivedUs=now;++reports;
  if(p[1]&1){++crcFailed;return true;} // Corrupt station/type and use flags are not evidence.
  lastStation=gnssbin::u16(p+4);lastType=gnssbin::u16(p+6);
  unsigned status=(p[1]>>1)&3;if(status==2)++used;else if(status==1)++unused;else ++unknown;return true;
 }
};
// SPSC byte FIFO: publish the whole record only after copying. Power-of-two size
// keeps positions correct across uint32_t wrap. Same 64 KiB payload budget as 8 fixed slots.
template<size_t N> struct FrameQueue {
 static_assert(N && !(N&(N-1)) && N<(1u<<31),"power-of-two capacity required");
 uint8_t data[N]{};std::atomic<uint32_t> head{0},tail{0},highWater{0};
 void copyIn(uint32_t offset,const void* src,size_t size){auto p=static_cast<const uint8_t*>(src);for(size_t i=0;i<size;i++)data[(offset+i)&(N-1)]=p[i];}
 void copyOut(uint32_t offset,void* dst,size_t size){auto p=static_cast<uint8_t*>(dst);for(size_t i=0;i<size;i++)p[i]=data[(offset+i)&(N-1)];}
 uint32_t queued()const{auto t=tail.load(std::memory_order_acquire);auto h=head.load(std::memory_order_acquire);return h-t>N?N:h-t;}
 template<class F> bool push(const F& f){
  if(!f.bytes||f.bytes>sizeof(f.data))return false;uint32_t h=head.load(std::memory_order_relaxed),used=h-tail.load(std::memory_order_acquire),size=10+f.bytes;
  if(size>N-used)return false;
  copyIn(h,&f.receivedUs,8);copyIn(h+8,&f.bytes,2);copyIn(h+10,f.data,f.bytes);
  if(used+size>highWater.load(std::memory_order_relaxed))highWater.store(used+size,std::memory_order_relaxed);
  head.store(h+size,std::memory_order_release);return true;
 }
 template<class F> bool pop(F& f){
  uint32_t t=tail.load(std::memory_order_relaxed);if(t==head.load(std::memory_order_acquire))return false;
  copyOut(t,&f.receivedUs,8);copyOut(t+8,&f.bytes,2);copyOut(t+10,f.data,f.bytes);tail.store(t+10+f.bytes,std::memory_order_release);return true;
 }
};
}
