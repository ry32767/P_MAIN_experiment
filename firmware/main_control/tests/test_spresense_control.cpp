#include "spresense_control_protocol.h"
#include "../firmware/spresense/libraries/AquaBeacon/src/AquaBeacon.h"
#include <assert.h>
#include <stdio.h>
static void exchange(sprwire::Uart& a,sprwire::Uart& b,const uint8_t* input,size_t n,unsigned phase=0){
 bool lowA=false,lowB=false;uint8_t output[128]{};unsigned count=0;
 for(unsigned t=0;t<16;t++){lowA=a.tick(true);lowB=b.tick(true);}
 assert(a.send(input,n));
 for(unsigned t=0;t<30000;t++){
  bool high=!(lowA||lowB);
  if(t%4==0)lowA=a.tick(high);
  if(t%4==phase)lowB=b.tick(!(lowA||lowB));
  int c;while((c=b.read())>=0){assert(count<n);output[count++]=uint8_t(c);}
  if(!a.busy()&&count==n)break;
 }
 assert(!a.busy());assert(count==n);assert(!memcmp(input,output,n));
 assert(b.framing==0 && b.overflow==0 && a.collisions==0);
}
int main(){
 {spnav::Nav n;assert(!sprctl::decodeCalibrationDiagnostics(n).available);
  aq::Nav emitted;emitted.satellites=0xa3;sprctl::encodeCalibrationDiagnostics(emitted,0x21,55);
  uint8_t frame[64];aq::encode(emitted,frame);assert(spnav::decode(frame,n));
  auto actual=sprctl::decodeCalibrationDiagnostics(n);assert(actual.available&&actual.rejection==0x21&&actual.resets==55);n={};
  n.satellites=0xa3;assert(!sprctl::decodeCalibrationDiagnostics(n).available);
  sprctl::encodeCalibrationDiagnostics(n,0x91,123);uint8_t payload[64];spnav::encode(n,payload);spnav::Nav decoded;
  assert(spnav::decode(payload,decoded));auto d=sprctl::decodeCalibrationDiagnostics(decoded);
  assert(d.available&&d.rejection==0x91&&d.resets==123);
  sprctl::encodeCalibrationDiagnostics(n,0xff,100000);d=sprctl::decodeCalibrationDiagnostics(n);assert(d.resets==65535&&d.rejection==255);
  n.flags|=spnav::position_valid;assert(!sprctl::decodeCalibrationDiagnostics(n).available);
  n.flags=0;n.satellites=0;assert(!sprctl::decodeCalibrationDiagnostics(n).available);
 }

 {uint8_t payload[sprctl::payload_size]{};assert(!sprctl::decodeSd(payload).available);
  sprctl::SdDiagnostics d;d.state=sprctl::SD_ERROR;d.failure=sprctl::SD_MOUNT;d.detail=-5;d.attempts=3;d.recoveries=42;d.syncedRows=123456;
  sprctl::encodeSd(d,payload);auto v=sprctl::decodeSd(payload);
  assert(v.available && v.state==d.state && v.failure==d.failure && v.detail==-5 && v.attempts==3 && v.recoveries==42 && v.syncedRows==123456);
  payload[89]=255;assert(!sprctl::decodeSd(payload).available);
 }

 sprctl::Message m;m.session=7;m.sequence=9;m.op=sprctl::RESET;
 uint8_t bytes[128];sprctl::encode(m,bytes);sprctl::Message d;
 assert(sprctl::decode(bytes,d));assert(d.op==sprctl::RESET);
 bytes[25]^=1;assert(!sprctl::decode(bytes,d));bytes[25]^=1;
 sprctl::Parser p;bool got=false;for(unsigned i=0;i<128;i++)got=p.push(bytes[i],1000+i*4400,d);assert(got);
 for(unsigned i=0;i<20;i++)p.push(bytes[i],500000+i*4400,d);
 got=false;for(unsigned i=0;i<128;i++)got=p.push(bytes[i],900000+i*4400,d);assert(got);
 sprctl::Replay replay;sprctl::Message reply=m;reply.kind=sprctl::RESPONSE;replay.save(m,reply);
 assert(replay.lookup(m,d)==1);m.data[0]=1;assert(replay.lookup(m,d)==-1);m.sequence++;assert(replay.lookup(m,d)==0);
 sprctl::Reference ref;sprctl::put64(m.data,1780000000000000ULL);m.data[8]=1;spnav::put32(m.data+12,3000000);
 assert(ref.apply(m,0xfffffff0ULL));assert(ref.at(0x100000010ULL)==1780000000000032ULL);
 assert(!ref.valid(0xfffffff0ULL+3000000));m.data[8]=4;assert(!ref.apply(m,1));m.data[8]=0;assert(ref.apply(m,1));assert(!ref.valid(1));
 for(unsigned phase=0;phase<4;phase++){
  sprwire::Uart a,b;uint8_t patterns[128];for(unsigned i=0;i<128;i++)patterns[i]=uint8_t(i*37);
  exchange(a,b,patterns,128,phase);exchange(b,a,patterns,128,phase);
 }
 sprwire::Uart stuck;for(unsigned i=0;i<3000;i++)stuck.tick(false);assert(stuck.read()<0);
 assert(stuck.send(bytes,128));for(unsigned i=0;i<3000;i++)assert(!stuck.tick(false));
 assert(stuck.busy());stuck.abort();assert(!stuck.tick(false));assert(!stuck.busy());
 sprwire::Uart full;
 for(unsigned i=0;i<260;i++){
  for(unsigned j=0;j<8;j++)full.tick(true);
  for(unsigned bit=0;bit<11;bit++)for(unsigned j=0;j<4;j++)full.tick(bit>=9);
 }
 assert(full.overflow>0);
 // Foreground scheduling gaps must not masquerade as gaps on the wire.
 sprwire::Uart sender,receiver;sprctl::Parser stamped,arrival;
 for(unsigned i=0;i<12;i++){sender.tick(true);receiver.tick(true);}
 assert(sender.send(bytes,sizeof(bytes)));bool lowSender=false,lowReceiver=false,stampedGot=false,arrivalGot=false;
 for(unsigned t=0;t<7000;t++){
  lowSender=sender.tick(!(lowSender||lowReceiver));lowReceiver=receiver.tick(!(lowSender||lowReceiver),9000000ULL+uint64_t(t)*sprwire::tick_us);
  if(t%1000==0){uint64_t received=0;int c;while((c=receiver.read(&received))>=0){
   assert(received>=9000000ULL);stampedGot=stamped.push(uint8_t(c),received,d)||stampedGot;
   arrivalGot=arrival.push(uint8_t(c),uint64_t(t)*sprwire::tick_us,d)||arrivalGot;
  }}
 }
 assert(stampedGot && !arrivalGot);
 puts("Spresense: frame CRC, parser recovery, replay, expiry/wrap, bidirectional open-drain UART PASS");
}
