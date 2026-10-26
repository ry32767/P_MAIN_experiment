#pragma once
#include "spresense_protocol.h"
#include "single_wire_uart.h"
namespace sprctl {
constexpr size_t frame_size=128,payload_size=104;
enum Kind:uint8_t { REQUEST=1,RESPONSE=2 };
enum Op:uint8_t { STATUS=1,STOP=2,START=3,RESET=4,CAL_START=5,CAL_STATUS=6,CAL_CLEAR=7,CAL_POSE=8,CAL_SAVE=9,CAL_END=10 };
enum Result:uint8_t { OK=0,BUSY=1,INVALID=2,UNSUPPORTED=3,FAILED=4,ACCEPTED=5,TIMEOUT=6 };
enum Calibration:uint8_t { CAL_NONE=0,CAL_WAITING=1,CAL_COLLECTING=2,CAL_MOVING=3,CAL_READY=4,CAL_SAVED=5,CAL_ERROR=6 };
// The PPS diagnostic Nav has no position. Its unused altitude carries an optional
// tagged calibration diagnostic; old nodes send zero and remain distinguishable.
struct CalibrationDiagnostics {bool available=false;uint8_t rejection=0;uint16_t resets=0;};
// Optional motion hint in PPS-only Nav.fix. Bits are sensor gyro axes, not body axes.
template<class Nav> inline void encodeMotion(Nav& nav,uint8_t axes){nav.fix=0xd0|(axes&7);}
inline bool motionAvailable(const spnav::Nav& nav){return nav.satellites==0xa3&&!(nav.flags&spnav::position_valid)&&(nav.fix&0xf8)==0xd0;}
template<class Nav> inline void encodeCalibrationDiagnostics(Nav& nav,uint8_t rejection,uint32_t resets){
 nav.altitude_mm=int32_t(0xca000000u|((resets>65535?65535:resets)<<8)|rejection);
}
inline CalibrationDiagnostics decodeCalibrationDiagnostics(const spnav::Nav& nav){
 CalibrationDiagnostics d;uint32_t value=uint32_t(nav.altitude_mm);
 if(nav.satellites!=0xa3 || (nav.flags&spnav::position_valid) || (value>>24)!=0xca)return d;
 d.available=true;d.rejection=uint8_t(value);d.resets=uint16_t(value>>8);return d;
}
// Optional versioned diagnostics at payload[88..103]; old firmware has no marker.
enum SdState:uint8_t { SD_STOPPED=0,SD_RECORDING=1,SD_RECOVERING=2,SD_ERROR=3 };
enum SdFailure:uint8_t { SD_NONE=0,SD_MOUNT=1,SD_NAMES=2,SD_PROBE_OPEN=3,SD_PROBE_IO=4,SD_LOG_OPEN=5,SD_HEADER=6,SD_WRITE=7,SD_SYNC=8,SD_FORMATTER=9 };
struct SdDiagnostics {
 bool available=false; uint8_t state=0,failure=0,attempts=0;
 int32_t detail=0;uint32_t recoveries=0,syncedRows=0;
};
inline void encodeSd(const SdDiagnostics& s,uint8_t* payload){
 payload[88]=0xd1;payload[89]=s.state;payload[90]=s.failure;payload[91]=s.attempts;
 spnav::put32(payload+92,uint32_t(s.detail));spnav::put32(payload+96,s.recoveries);spnav::put32(payload+100,s.syncedRows);
}
inline SdDiagnostics decodeSd(const uint8_t* payload){
 SdDiagnostics s;if(payload[88]!=0xd1 || payload[89]>SD_ERROR || payload[90]>SD_FORMATTER)return s;
 s.available=true;s.state=payload[89];s.failure=payload[90];s.attempts=payload[91];
 s.detail=int32_t(spnav::get32(payload+92));s.recoveries=spnav::get32(payload+96);s.syncedRows=spnav::get32(payload+100);return s;
}
inline const char* sdStateName(uint8_t s){switch(s){case SD_RECORDING:return "RECORDING";case SD_RECOVERING:return "RECOVERING";case SD_ERROR:return "ERROR";default:return "STOPPED";}}
inline const char* sdFailureName(uint8_t s){switch(s){case SD_MOUNT:return "MOUNT";case SD_NAMES:return "NAMES";case SD_PROBE_OPEN:return "PROBE_OPEN";case SD_PROBE_IO:return "PROBE_IO";case SD_LOG_OPEN:return "LOG_OPEN";case SD_HEADER:return "HEADER";case SD_WRITE:return "WRITE";case SD_SYNC:return "SYNC";case SD_FORMATTER:return "FORMATTER";default:return "NONE";}}
struct Message {
 uint8_t kind=REQUEST,op=STATUS,result=OK;uint32_t session=0,sequence=0;
 uint8_t data[payload_size]{};
};
inline void put64(uint8_t* p,uint64_t n){spnav::put32(p,uint32_t(n));spnav::put32(p+4,uint32_t(n>>32));}
inline uint64_t get64(const uint8_t* p){return spnav::get32(p)|uint64_t(spnav::get32(p+4))<<32;}
inline void encode(const Message& m,uint8_t* b){
 memset(b,0,frame_size);memcpy(b,"ABH3",4);b[4]=3;b[5]=m.kind;b[6]=m.op;b[7]=m.result;
 spnav::put32(b+8,m.session);spnav::put32(b+12,m.sequence);memcpy(b+20,m.data,payload_size);
 spnav::put32(b+124,spnav::crc32(b,124));
}
inline bool decode(const uint8_t* b,Message& m){
 if(memcmp(b,"ABH3",4)||b[4]!=3||b[5]<REQUEST||b[5]>RESPONSE||b[6]<STATUS||b[6]>CAL_END||b[7]>TIMEOUT
   ||spnav::get32(b+16)!=0||spnav::get32(b+124)!=spnav::crc32(b,124))return false;
 m.kind=b[5];m.op=b[6];m.result=b[7];m.session=spnav::get32(b+8);m.sequence=spnav::get32(b+12);memcpy(m.data,b+20,payload_size);return m.session && m.sequence;
}
class Parser {
 uint8_t b[frame_size]{};unsigned used=0;uint64_t last=0;
public:
 uint32_t errors=0;
 void reset(){used=0;last=0;}
 bool push(uint8_t c,uint64_t now,Message& m){
  if(used && now-last>20000)used=0;last=now;b[used++]=c;
  if(used<frame_size)return false;
  if(decode(b,m)){used=0;return true;}
  if(!memcmp(b,"ABH3",4))++errors;
  memmove(b,b+1,--used);return false;
 }
};
// UTC is labelled at nominal reception of the final byte. Without D02/PPS,
// timer jitter and foreground latency are unmeasured; this is reference UTC.
struct Reference {
 uint64_t mono=0,epoch=0,until=0;uint8_t source=0;
 bool valid(uint64_t now)const{return source>=1 && source<=3 && now>=mono && now<until;}
 uint64_t at(uint64_t now)const{return valid(now)?epoch+now-mono:0;}
 bool apply(const Message& m,uint64_t now){
  uint64_t utc=get64(m.data);uint32_t life=spnav::get32(m.data+12);
  if(m.data[8]==0){*this={};return true;}
  if(m.data[8]>3 || utc<1577836800000000ULL || utc>=4102444800000000ULL || !life || life>5000000)return false;
  mono=now;epoch=utc;until=now+life;source=m.data[8];return true;
 }
};
// Cache full request equality: sequence reuse with different content is rejected.
class Replay {
 struct Entry {Message request,response;bool have=false;};
 Entry entries[2];unsigned replace=0;
public:
 int lookup(const Message& m,Message& out)const{
  for(const auto& e:entries)if(e.have && m.session==e.request.session){
   if(m.sequence!=e.request.sequence)return int32_t(m.sequence-e.request.sequence)<0?-1:0;
   if(m.op!=e.request.op || m.result!=e.request.result || memcmp(m.data,e.request.data,payload_size))return -1;
   out=e.response;return 1;
  }
  return 0;
 }
 void save(const Message& m,const Message& out){
  for(auto& e:entries)if(e.have && e.request.session==m.session){e.request=m;e.response=out;return;}
  auto& e=entries[replace++%2];e.request=m;e.response=out;e.have=true;
 }
};
}
