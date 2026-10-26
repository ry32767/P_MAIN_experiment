#pragma once
#include "spresense_control_protocol.h"
#include "spresense_calibration.h"
utcclock::Anchor utcSnapshot();
struct SpresenseStatus {
 spnav::Nav nav;
 sprctl::SdDiagnostics sd;
 sprctl::CalibrationDiagnostics calibrationDiagnostics;
 uint64_t sampledUs=0,receivedUs=0,utcUs=0;
 uint32_t packets=0,crcErrors=0,rejected=0,overflows=0,bytes=0,timeouts=0,framing=0,collisions=0,commandId=0;
 uint8_t commandOp=0,commandResult=sprctl::OK,calibration=0,timeSource=0,lastOperation=0,lastResult=0;
 uint8_t calibrationMask=0,calibrationPose=255,calibrationProgress=0,calibrationSaveError=0,calibrationRestoreError=0;
 uint32_t calibrationGeneration=0;float calibrationGravity=0,calibrationLatitude=0;bool calibrationApplied=false;
 bool ppsFresh=false,ppsLocked=false,imuTimeReady=false,timeDiagnostics=false;uint32_t ppsCount=0,ppsPeriodUs=0;
 bool synchronized=false,recording=false,commandPending=false,enabled=false;
 bool motionAvailable=false;uint8_t motionAxes=0;
};
SpresenseStatus sharedSpresense;
sprwire::Uart spresenseWire;
aq::Ring<sprctl::Message,4> spresenseCommands;
std::atomic<bool> spresenseCommandOutstanding{false};
std::atomic<uint32_t> spresenseCommandId{0};
repeating_timer_t spresenseTimer;
std::atomic<uint32_t> spresenseTimerTicks{0};
bool spresenseEnabled=false;
bool spresenseTimerTick(repeating_timer_t*) {
 ++spresenseTimerTicks;
 static bool low=false;
 bool next=spresenseWire.tick(gpio_get(PIN_SPRESENSE_DATA),time_us_64());
 if(next!=low){gpio_set_dir(PIN_SPRESENSE_DATA,next?GPIO_OUT:GPIO_IN);low=next;}
 return true;
}
void initSpresense(){
 // GP6 belongs exclusively to initGnss(); this transport never changes it.
 gpio_init(PIN_SPRESENSE_DATA);gpio_put(PIN_SPRESENSE_DATA,0);gpio_set_dir(PIN_SPRESENSE_DATA,GPIO_IN);
 spresenseEnabled=add_repeating_timer_us(-int(sprwire::tick_us),spresenseTimerTick,nullptr,&spresenseTimer);
}
bool requestSpresense(uint8_t op,uint32_t& id,float gravity=9.80665f,float latitude=0,uint8_t pose=255){
 if(op<sprctl::STOP || op>sprctl::CAL_END || spresenseCommandOutstanding.exchange(true))return false;
 sprctl::Message m;m.op=op;m.sequence=++spresenseCommandId;if(!m.sequence)m.sequence=++spresenseCommandId;
 sprcal::putFloat(m.data+16,gravity);sprcal::putFloat(m.data+20,latitude);m.data[24]=pose;
 if(!spresenseCommands.push(m)){spresenseCommandOutstanding=false;return false;}
 id=m.sequence;return true;
}
std::atomic<uint32_t> parentSetGeneration{0};
void pollSpresense(){
 static sprctl::Parser parser;static SpresenseStatus s;static sprctl::Message waiting;
 static uint32_t seq=0;static uint64_t nextPoll=0,deadline=0;static bool active=false,userCommand=false;static unsigned retries=0;
 uint64_t now=time_us_64();s.enabled=spresenseEnabled;
 sprctl::Message response;
 for(unsigned budget=0;budget<256;budget++){
  uint64_t receivedTickUs=0;int c=spresenseWire.read(&receivedTickUs);if(c<0)break;++s.bytes;
  if(!parser.push(uint8_t(c),receivedTickUs,response))continue;
  if(!active || response.kind!=sprctl::RESPONSE || response.session!=waiting.session || response.sequence!=waiting.sequence || response.op!=waiting.op){++s.rejected;continue;}
  spnav::Nav nav;
 sprctl::SdDiagnostics sd;
  if(!spnav::decode(response.data,nav)){++s.rejected;continue;}
  s.sd=sprctl::decodeSd(response.data);
  s.calibrationDiagnostics=sprctl::decodeCalibrationDiagnostics(nav);
  s.nav=nav;s.receivedUs=now;++s.packets;s.recording=response.data[64];s.calibration=response.data[65];
  s.synchronized=response.data[66]&1;s.timeDiagnostics=nav.satellites==0xa3;s.ppsFresh=s.timeDiagnostics&&(response.data[66]&2);s.ppsLocked=s.timeDiagnostics&&(response.data[66]&4);s.imuTimeReady=s.timeDiagnostics&&(response.data[66]&8);s.ppsCount=s.timeDiagnostics?uint32_t(nav.lat_e7):0;s.ppsPeriodUs=s.timeDiagnostics?uint32_t(nav.lon_e7):0;s.timeSource=response.data[67];s.lastOperation=response.data[68];s.lastResult=response.data[69];s.utcUs=nav.utc_s*1000000ULL+nav.usec;
  s.calibrationMask=response.data[70];s.calibrationPose=response.data[71];s.calibrationProgress=response.data[72];s.calibrationApplied=response.data[73];
  s.calibrationSaveError=response.data[74];s.calibrationRestoreError=response.data[75];s.calibrationGeneration=spnav::get32(response.data+76);
  s.calibrationGravity=sprcal::getFloat(response.data+80);s.calibrationLatitude=sprcal::getFloat(response.data+84);
  s.motionAvailable=sprctl::motionAvailable(nav);s.motionAxes=s.motionAvailable?(nav.fix&7):0;
  if(userCommand){s.commandId=waiting.sequence;s.commandOp=waiting.op;s.commandResult=response.result;spresenseCommandOutstanding=false;}
  active=false;nextPoll=now+1000000;
 }
 if(active && now>=deadline && userCommand && retries<2 && !spresenseWire.busy()){
  // Full byte-for-byte replay of the same session/sequence/payload. The node
  // reuses its cached response; it never repeats the operation or refreshes UTC.
  uint8_t frame[sprctl::frame_size];sprctl::encode(waiting,frame);
  if(spresenseWire.send(frame,sizeof(frame))){++retries;deadline=now+3200000;parser.reset();}
 }
 if(active && now>=deadline){
  ++s.timeouts;if(userCommand){s.commandId=waiting.sequence;s.commandOp=waiting.op;s.commandResult=sprctl::TIMEOUT;spresenseCommandOutstanding=false;}
  spresenseWire.abort();active=false;nextPoll=now+1000000;parser.reset();
 }
 if(s.enabled && !active && !spresenseWire.busy()){
  sprctl::Message m;bool requested=spresenseCommands.pop(m);
  if(requested || now>=nextPoll){
   if(!requested){m.op=sprctl::STATUS;m.sequence=++seq;if(!m.sequence)m.sequence=++seq;}
   m.session=requested?sessionId:(sessionId^0x53505232u);if(!m.session)m.session=1;
   auto gn=gnssSnapshot();auto liveGps=utcclock::ppsAnchor(gn.labelledPpsUs,gn.labelledUtcUs,now,gnssLocked(gn,now)&&spresenseGpsEnabled());
   // Core 0 can be busy mounting SD. Core 1 has current GNSS/PPS and must not
   // depend on a stale UI/storage UTC snapshot for the sensor time transfer.
   auto utc=liveGps.valid(now)?liveGps:utcSnapshot();uint64_t completion=now+sprwire::receive_us+sprwire::tick_us;
   uint64_t epoch=utc.at(now);if(epoch)epoch+=completion-now;sprctl::put64(m.data,epoch);m.data[8]=epoch?utc.source:0;
   uint32_t life=epoch?5000000:0;
   spnav::put32(m.data+12,life);
   if(liveGps.valid(now)){
    m.data[25]=0xa3;sprctl::put64(m.data+26,gn.labelledUtcUs);sprctl::put64(m.data+34,now-gn.labelledPpsUs);
   }
   spnav::put32(m.data+46,parentSetGeneration.load());spnav::put32(m.data+50,sessionId);
   uint8_t frame[sprctl::frame_size];sprctl::encode(m,frame);
   if(spresenseWire.send(frame,sizeof(frame))){waiting=m;active=true;userCommand=requested;deadline=now+3200000;retries=0;parser.reset();}
   else if(requested)spresenseCommandOutstanding=false;
  }
 }
 s.sampledUs=now;s.crcErrors=parser.errors;s.overflows=spresenseWire.overflow;s.framing=spresenseWire.framing;s.collisions=spresenseWire.collisions;s.commandPending=spresenseCommandOutstanding;
 mutex_enter_blocking(&stateMutex);sharedSpresense=s;mutex_exit(&stateMutex);
}
SpresenseStatus spresenseSnapshot(){mutex_enter_blocking(&stateMutex);auto s=sharedSpresense;mutex_exit(&stateMutex);return s;}
bool spresenseFresh(const SpresenseStatus& s,uint64_t now){return s.packets && now>=s.receivedUs && now-s.receivedUs<5000000;}
bool spresenseUsb(char c){
 static char line[80];static unsigned used=0;static bool collecting=false,overflow=false;
 if(c=='@' && !collecting){collecting=true;used=0;overflow=false;return true;}
 if(!collecting)return false;
 if(c!='\n'){if(c!='\r'){if(used+1<sizeof(line))line[used++]=c;else overflow=true;}return true;}
 collecting=false;line[used]=0;unsigned op=0,pose=255;float gravity=9.80665f,latitude=0;char trailing;
 if(!overflow && parentCalibrationUsb(line))return true;
 int fields=sscanf(line,"SPR %u %f %f %u %c",&op,&gravity,&latitude,&pose,&trailing);
 auto s=spresenseSnapshot();uint32_t id=0;
 bool ok=!overflow && (fields==1||fields==4) && op>=sprctl::STOP && op<=sprctl::CAL_END && pose<=255 && spresenseFresh(s,time_us_64());
 if(op==sprctl::CAL_START)ok=ok && fields==4 && sprcal::config(gravity,latitude);
 if(op==sprctl::CAL_POSE)ok=ok && fields==4 && pose<6;
 if(op==sprctl::RESET)ok=ok && !s.recording && s.lastOperation==sprctl::STOP && s.lastResult==sprctl::OK;
 ok=ok && requestSpresense(uint8_t(op),id,gravity,latitude,uint8_t(pose));
 Serial.printf("# SPR_USB accepted=%u command_id=%lu op=%u\n",unsigned(ok),(unsigned long)id,op);return true;
}
