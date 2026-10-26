#include "gnss_diagnostics.h"
#include <cassert>
#include <cstdio>
#include <thread>
#include <vector>
struct Frame {uint64_t receivedUs=0;uint16_t bytes=0;uint8_t data[8192]{};};
int main(){
 gnssdiag::Settings settings;uint8_t p[100]={1,0,0,0};size_t n=4;
 for(unsigned j=0;j<7;j++){auto k=gnssdiag::keys[j];for(unsigned i=0;i<4;i++)p[n++]=k>>(8*i);p[n++]=j==6?3:1;}
 assert(settings.read(p,n,100)&&settings.complete()&&settings.values[2]==1&&settings.values[6]==3);
 auto before=settings;assert(!settings.read(p,n-1,200));assert(settings.receivedUs==before.receivedUs);
 gnssdiag::L5Enable policy;assert(!policy.needed(settings));settings.values[2]=0;assert(policy.needed(settings));
 policy.sent(0xffffff00u);assert(policy.blocked(0x00000001u));policy.ack(true,0xffffff80u);
 assert(policy.blocked(uint32_t(0xffffff80u+699)));assert(!policy.blocked(uint32_t(0xffffff80u+700)));assert(!policy.needed(settings));
 settings.values[2]=1;policy.readback(settings);assert(policy.verified&&!policy.failed);
 gnssdiag::L5Enable timeout;timeout.sent(100);assert(timeout.blocked(2099));assert(!timeout.blocked(2100)&&timeout.failed);
 gnssdiag::L5Enable nak;nak.sent(100);nak.ack(false,101);assert(nak.failed&&!nak.blocked(102));
 p[0]=0;assert(!settings.read(p,n,200));p[0]=1;p[1]=1;assert(!settings.read(p,n,200));p[1]=0;
 p[6]=0xff;assert(settings.read(p,n,300));assert(settings.known==127); // Unknown key cannot erase read values.
 gnssdiag::Signals sig;uint8_t v[8+16*4]{};v[5]=4;
 auto entry=[&](unsigned i,uint8_t g,uint8_t sv,uint8_t signal,uint8_t quality,uint16_t flags){auto q=v+8+16*i;q[0]=g;q[1]=sv;q[2]=signal;q[6]=35;q[7]=quality;q[10]=flags;q[11]=flags>>8;};
 entry(0,2,13,0,7,0x90);entry(1,2,13,4,7,0x90);entry(2,0,9,7,4,0);entry(3,0,10,7,1,0);
 assert(sig.read(v,sizeof(v),50)&&sig.galDualPhase==1&&sig.gpsL5==1&&sig.galE1==1&&sig.galE5a==1&&sig.phaseCorrected==2);
 assert(!sig.read(v,sizeof(v)-1,60)&&sig.receivedUs==50);v[4]=1;assert(!sig.read(v,sizeof(v),60));
 gnssdiag::Corrections c;uint8_t rc[8]={2,4,0,0,5,0,0x4d,4};assert(c.read(rc,8,1)&&c.used==1&&c.lastType==1101&&c.lastStation==5);
 rc[1]=2;assert(c.read(rc,8,2)&&c.unused==1);rc[1]=0;assert(c.read(rc,8,3)&&c.unknown==1);
 rc[1]=5;rc[4]=99;assert(c.read(rc,8,4)&&c.crcFailed==1&&c.used==1&&c.lastStation==5);
 assert(!c.read(rc,7,5)&&c.reports==4);rc[0]=0;assert(!c.read(rc,8,5));
 static gnssdiag::FrameQueue<65536> q;Frame a,b;a.bytes=48;a.receivedUs=123;memset(a.data,0x62,a.bytes);
 // More short frames than the old eight-slot queue, without additional 64 KiB slots.
 for(unsigned i=0;i<128;i++){a.receivedUs=i;assert(q.push(a));}assert(q.highWater==128*58);
 for(unsigned i=0;i<128;i++){assert(q.pop(b)&&b.receivedUs==i&&b.bytes==48&&!memcmp(a.data,b.data,48));}assert(!q.pop(b));
 q.head=0xfffffff0u;q.tail=0xfffffff0u;a.bytes=8192;memset(a.data,0x95,a.bytes);
 for(unsigned i=0;i<7;i++){a.receivedUs=i;assert(q.push(a));}assert(!q.push(a));
 for(unsigned i=0;i<7;i++){assert(q.pop(b)&&b.receivedUs==i&&b.bytes==8192&&!memcmp(a.data,b.data,8192));}assert(q.queued()==0);
 a.bytes=0;assert(!q.push(a));
 // Real producer/consumer contention: records must remain whole and ordered.
 std::thread producer([&]{Frame f;for(unsigned i=0;i<20000;i++){f.bytes=16+i%8000;f.receivedUs=i;memset(f.data,uint8_t(i),f.bytes);while(!q.push(f))std::this_thread::yield();}});
 for(unsigned i=0;i<20000;i++){while(!q.pop(b))std::this_thread::yield();assert(b.receivedUs==i&&b.bytes==16+i%8000);for(unsigned j=0;j<b.bytes;j++)assert(b.data[j]==uint8_t(i));}
 producer.join();assert(!q.pop(b));
 puts("PASS GNSS RAM readback, malformed/stale response protection, signal/RTCM flags, compact FIFO overflow/wrap and concurrent integrity");
}
