#include "protocol.h"
#include "control.h"
#include "ranging.h"
#include <cassert>
#include <iostream>
#include <limits>
int main(){
 // Ranging uses one child's clock, including close arrivals and 32-bit rollover.
 uint64_t ref=0xffffffffull-100;
 aq::AcousticWindow w;w.start(ref);w.edge(ref-1,true);w.edge(ref+1,false);assert(!w.confirmed);
 w.edge(ref+50,true);w.edge(ref+55,false);assert(!w.confirmed);
 w.edge(ref+100,true);w.edge(ref+106,false);assert(w.confirmed==ref+100);
 assert(aq::localTof(ref,w.confirmed)==100);assert(aq::rangeStatus(ref,w.confirmed,false,false)==2);
 assert(aq::rangeStatus(ref,ref,false,false)==2);
 assert(aq::rangeStatus(ref,ref+200,false,false)==3);
 assert(aq::rangeStatus(ref,ref+aq::windowUs,false,false)==3);
 assert(aq::rangeStatus(ref,ref+aq::windowUs+1,false,false)==4);
 assert(aq::rangeStatus(ref,ref-1,false,false)==4);
 assert(aq::rangeStatus(0,ref,false,false)==4);
 assert(aq::rangeStatus(ref,ref+500,true,false)==4);
 assert(aq::rangeStatus(ref,0,false,false)==1);assert(aq::rangeStatus(ref,0,false,true)==5);
 w.start(ref);w.edge(ref+aq::windowUs+1,true);w.edge(ref+aq::windowUs+7,false);assert(!w.confirmed);
 assert(!aq::canStartProbe(ref,ref+1,0,0,false));
 assert(!aq::canStartProbe(ref,ref,ref+500,0,false));
 assert(!aq::canStartProbe(ref,ref,0,ref-100,false));
 assert(!aq::canStartProbe(ref,ref,0,0,true));
 assert(aq::canStartProbe(ref+1000000,ref,0,0,false));
 // Pattern recognition: noise, missing/incorrect spacing, earliest pulse timestamp, full window and overflow.
 aq::AcquisitionConfig cfg;cfg.rate=4;cfg.pattern=1;
 assert(aq::validConfig(cfg) && aq::shotIntervalUs(cfg)==250000);
 cfg.rate=0;assert(!aq::validConfig(cfg));cfg.rate=5;assert(!aq::validConfig(cfg));cfg.rate=4;
 cfg.pattern=2;assert(!aq::validConfig(cfg));cfg.pattern=1;
 for(auto hz:aq::frequencies){
  cfg.frequency=hz;auto d1=aq::patternSpacingUs(hz,aq::patternGapUs(cfg.patternId,0)),d2=aq::patternSpacingUs(hz,aq::patternGapUs(cfg.patternId,1));
  auto pulse=[&](uint64_t t){w.edge(t,true);w.edge(t+6,false);};
  w.start(ref,cfg);pulse(ref+100);pulse(ref+100+d1);assert(!w.confirmed);
  pulse(ref+100+d1+d2);assert(w.confirmed==ref+100);
  w.start(ref,cfg);pulse(ref+100);pulse(ref+100+d1);pulse(ref+100+2*d1);assert(!w.confirmed);
  w.start(ref,cfg);pulse(ref+22);pulse(ref+aq::windowUs);pulse(ref+aq::windowUs+d1);pulse(ref+aq::windowUs+d1+d2);assert(w.confirmed==ref+aq::windowUs);
 }
 w.start(ref,cfg);for(unsigned i=0;i<65;i++){w.edge(ref+10+i*10,true);w.edge(ref+16+i*10,false);}assert(w.overflow && !w.confirmed);
 // All expected/received pairs: one older pattern alone must not match this shot.
 aq::PatternSequence sequence(3);uint64_t planned=100000000;
 for(unsigned i=0;i<64;i++){assert(sequence.available(planned));sequence.reserve(planned);planned+=250000;}
 assert(sequence.cursor==3);assert(!sequence.available(115999999));assert(sequence.available(116000000));
 sequence.reserve(116000000);assert(sequence.cursor==20);assert(!sequence.available(100));
 bool used[aq::patternCount]{};uint8_t id=3;
 for(unsigned i=0;i<aq::patternCount;i++){assert(!used[id]);used[id]=true;id=aq::nextPatternId(id);}assert(id==3);
 for(auto hz:aq::frequencies)for(unsigned expected=0;expected<aq::patternCount;expected++){
  cfg.frequency=hz;cfg.patternId=expected;
  auto g1=aq::patternGapUs(expected,0),g2=aq::patternGapUs(expected,1);
  assert(aq::validShotPattern(cfg,g1,g2));assert(!aq::validShotPattern(cfg,g1+1,g2));
  for(unsigned received=0;received<aq::patternCount;received++){
   w.start(ref,cfg);uint64_t t=ref+100;
   auto pulse=[&](uint64_t when){w.edge(when,true);w.edge(when+6,false);};
   pulse(t);t+=aq::patternSpacingUs(hz,aq::patternGapUs(received,0));pulse(t);
   t+=aq::patternSpacingUs(hz,aq::patternGapUs(received,1));pulse(t);
   assert(bool(w.confirmed)==(expected==received));
  }
  // A delayed old packet's acoustic train then the current train.
  w.start(ref,cfg);auto pulse=[&](uint64_t when){w.edge(when,true);w.edge(when+6,false);};
  uint8_t prior=(expected+47)%64;uint64_t t=ref+10;pulse(t);
  t+=aq::patternSpacingUs(hz,aq::patternGapUs(prior,0));pulse(t);
  t+=aq::patternSpacingUs(hz,aq::patternGapUs(prior,1));pulse(t);assert(!w.confirmed);
  auto first=ref+aq::windowUs;t=first;pulse(t);
  t+=aq::patternSpacingUs(hz,g1);pulse(t);t+=aq::patternSpacingUs(hz,g2);pulse(t);assert(w.confirmed==first);
 }
 cfg.patternId=64;assert(!aq::validShotPattern(cfg,1000,7000));
 cfg.pattern=0;cfg.patternId=aq::noPattern;assert(aq::validShotPattern(cfg,0,0));assert(!aq::validShotPattern(cfg,1,0));
 aq::Packet plannedShot;plannedShot.pattern=1;plannedShot.patternId=17;plannedShot.patternGap1=1600;plannedShot.patternGap2=8200;
 auto reply=plannedShot;assert(aq::shotMetadataMatches(reply,plannedShot));
 reply.patternId++;assert(!aq::shotMetadataMatches(reply,plannedShot));reply=plannedShot;
 reply.patternGap1++;assert(!aq::shotMetadataMatches(reply,plannedShot));reply=plannedShot;
 reply.patternGap2++;assert(!aq::shotMetadataMatches(reply,plannedShot));reply=plannedShot;
 reply.frequency=50000;assert(!aq::shotMetadataMatches(reply,plannedShot));reply=plannedShot;
 reply.pattern=0;assert(!aq::shotMetadataMatches(reply,plannedShot));
 cfg.pattern=1;cfg.patternId=17;cfg.frequency=200000;
 for(int error:{-201,-200,200,201}){
  w.start(ref,cfg);uint64_t t=ref+100;w.edge(t,true);w.edge(t+6,false);
  t+=aq::patternSpacingUs(cfg.frequency,aq::patternGapUs(17,0))+error;w.edge(t,true);w.edge(t+6,false);
  t+=aq::patternSpacingUs(cfg.frequency,aq::patternGapUs(17,1));w.edge(t,true);w.edge(t+6,false);
  assert(bool(w.confirmed)==(error>=-200 && error<=200));
 }
 aq::Packet v4;v4.kind=aq::SCHEDULE;v4.version=4;aq::seal(v4);assert(!aq::valid(v4));
 std::cout<<"PASS 64-pattern cycle, all 16384 expected/received pairs, delayed old train then current, gap metadata and v4 rejection\n";
 std::cout<<"PASS rate/pattern bounds, pattern noise/missing/wrong spacing, window tail and candidate overflow\n";
 aq::Packet old;old.kind=aq::RESULT;old.version=1;aq::seal(old);assert(!aq::valid(old));
 std::cout<<"PASS child-local ToF, close/zero distance, negative/out-of-window rejection, edge width, overflow flag, missing reference, old version rejection\n";
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
