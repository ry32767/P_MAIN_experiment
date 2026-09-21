#include "protocol.h"
#include "control.h"
#include <cassert>
#include <iostream>
#include <limits>
int main(){
 aq::Control ctl;ctl.op=aq::READ_LOG;ctl.arg=0xfffffff0;ctl.length=104;for(unsigned i=0;i<104;i++)ctl.data[i]=i;
 aq::Packet cp;cp.kind=aq::CONTROL;cp.session=123;cp.seq=456;aq::pack(cp,ctl);aq::seal(cp);assert(aq::valid(cp));auto decoded=aq::unpack(cp);assert(decoded.arg==ctl.arg && decoded.length==104 && !memcmp(decoded.data,ctl.data,104));assert(cp.session==123 && cp.seq==456);
 assert(aq::safeFilename("C_MAIN_IMU_100Hz_00000.csv"));assert(!aq::safeFilename("../file.csv"));assert(!aq::safeFilename("/file.csv"));assert(!aq::safeFilename("a\\b.csv"));char longname[65];memset(longname,'a',64);longname[64]=0;assert(!aq::safeFilename(longname));
 assert(aq::crc32("123456789",9)==0xcbf43926);
 aq::Packet p;p.kind=aq::PROBE;p.seq=0xffffffff;p.t1=0x100000001ull;p.offset=-123456789;aq::seal(p);assert(aq::valid(p));
 aq::Parser parser;aq::Packet out;assert(!parser.push(0,out));assert(!parser.push(0x51,out));assert(!parser.push(0x00,out));
 auto b=reinterpret_cast<uint8_t*>(&p);for(size_t i=0;i<sizeof(p);i++)assert(parser.push(b[i],out)==(i+1==sizeof(p)));assert(out.t1==p.t1 && out.offset==p.offset);
 b[20]^=1;for(size_t i=0;i<sizeof(p);i++)assert(!parser.push(b[i],out));assert(parser.errors==1);b[20]^=1;
 for(size_t i=0;i<sizeof(p);i++)parser.push(b[i],out);assert(aq::valid(out));
 aq::Ring<int,4> q;for(int k=0;k<4;k++)assert(q.push(k));assert(!q.push(8));int n;for(int k=0;k<4;k++){assert(q.pop(n));assert(n==k);}assert(!q.pop(n));
 q.head=std::numeric_limits<uint32_t>::max()-1;q.tail=q.head.load();for(int k=0;k<4;k++)assert(q.push(k));for(int k=0;k<4;k++){assert(q.pop(n));assert(n==k);}
 aq::Sync s;uint64_t a=0x100000000ull;int64_t offset=-250000;uint32_t air=aq::airUs;
 s.update(a,a+air+offset,a+air+offset+3000,a+2*air+3000);assert(s.ok && s.offset==offset && s.uncertainty==100);
 s.update(a,a+air+offset+200,a+air+offset+3200,a+2*air+3600);assert(s.ok);assert(s.uncertainty>=uint32_t(llabs(s.offset-offset)));
 s.update(a,10,9,a+air*2);assert(!s.ok);s.update(a,10,20,a-1);assert(!s.ok);
 std::cout<<"PASS CRC, parser resync, 64-bit time, signed offset, queue full/empty/wrap, timing bounds; packet_bytes="<<sizeof(p)<<" air_us="<<air<<"\n";
}
