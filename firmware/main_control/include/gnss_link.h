#pragma once
#include "gnss_nmea.h"
#include "gnss_binary.h"
#include "gnss_diagnostics.h"
struct GnssStatus {
 spnav::Nav nav;uint64_t sampledUs=0,receivedUs=0,ppsUs=0,utcUs=0,lastSentenceUs=0;
 uint32_t packets=0,errors=0,bytes=0,ppsCount=0,periodUs=0,uartAgeUs=0,rejected=0,baud=GNSS_BAUD_FIRST;
 uint64_t labelledPpsUs=0,labelledUtcUs=0;
 bool synchronized=false, f9p=false, configAck=false,configVerified=false;
 char model[64]{},gga[160]{};
 uint64_t pvtUs=0,ggaUs=0,rawxUs=0,sfrbxUs=0;
 int32_t latE7=0,lonE7=0,heightMm=0;uint32_t hAccMm=0;
 uint32_t rawx=0,sfrbx=0,ubxErrors=0,uartOverflows=0,configNak=0;
 uint8_t carrier=0;bool positionValid=false;
 gnssdiag::L5Enable l5Enable;gnssdiag::Settings signalSettings;gnssdiag::Signals signals;gnssdiag::Corrections corrections;

};
GnssStatus sharedGnss;
struct GnssFrame {uint64_t receivedUs=0;uint16_t bytes=0;uint8_t data[gnssbin::maxUbx];};
struct CorrectionFrame {uint32_t generation=0;uint16_t bytes=0;uint8_t data[gnssbin::maxRtcm];};
gnssdiag::FrameQueue<65536> gnssFrames;
aq::Ring<CorrectionFrame,16> correctionIn;
aq::Ring<CorrectionFrame,32> correctionLogs;
std::atomic<uint32_t> gnssLogDropped{0},correctionLogDropped{0},correctionTxDropped{0},correctionTxFrames{0};
std::atomic<uint32_t> gnssRawxDropped{0},gnssSfrbxDropped{0},gnssDiagnosticDropped{0};
std::atomic<bool> gnssRecording{false};
std::atomic<uint32_t> correctionGeneration{0};
static uint8_t gnssCommand[128];static unsigned gnssCommandUsed=0,gnssCommandSent=0;
static bool gnssSend(uint8_t cls,uint8_t id,const uint8_t* payload=nullptr,uint16_t len=0){if(gnssCommandSent<gnssCommandUsed||len>120)return false;gnssCommandUsed=gnssbin::encode(cls,id,payload,len,gnssCommand);gnssCommandSent=0;return true;}
static void gnssCommandTick(){for(unsigned budget=0;budget<32&&gnssCommandSent<gnssCommandUsed&&uart_is_writable(uart1);budget++)Serial2.write(gnssCommand[gnssCommandSent++]);}

volatile uint64_t gnssPpsUs=0;volatile uint32_t gnssPpsCount=0;
void onGnssPps(){gnssPpsUs=time_us_64();++gnssPpsCount;}
void initGnss(){
 pinMode(PIN_GNSS_PPS,INPUT);attachInterrupt(digitalPinToInterrupt(PIN_GNSS_PPS),onGnssPps,RISING);
 Serial2.setTX(PIN_GNSS_TX);Serial2.setRX(PIN_GNSS_RX);Serial2.setFIFOSize(16384);Serial2.begin(GNSS_BAUD_FIRST);
}
void pollGnss(){
 gnssCommandTick();
 static gnss::Parser parser;static gnssbin::UbxParser ubx;static GnssFrame logFrame;static CorrectionFrame corr;static bool haveCorr=false;static unsigned corrOffset=0;static char nmeaLine[160];static unsigned nmeaUsed=0;static uint32_t configAt=0;static uint8_t configStage=0;static gnss::Clock clock;static GnssStatus s;static uint32_t seen=0,baudIndex=0,lastSwitch=millis();
 static uint64_t lastMonPoll=0;static uint32_t lastSignalPoll=0,lastSettingsPoll=0;
 static const uint32_t bauds[]={GNSS_BAUD_FIRST,GNSS_BAUD_SECOND,115200,230400,57600};
 uint32_t irq=save_and_disable_interrupts();auto edge=gnssPpsUs;auto count=gnssPpsCount;restore_interrupts(irq);
 if(count!=seen){if(count-seen>1)clock.invalidate();clock.pps(edge);seen=count;s.ppsUs=edge;s.ppsCount=count;}
 if(Serial2.overflow()){++s.uartOverflows;ubx.reset();parser.reset();clock.invalidate();nmeaUsed=0;}
 spnav::Nav nav;
 for(unsigned budget=0;budget<4096&&Serial2.available();++budget){
  uint32_t before=parser.packets;++s.bytes;uint8_t c=Serial2.read();bool binary=ubx.active() || c==0xb5;
  if(ubx.push(c)){
   const uint8_t* f=ubx.frame;const uint8_t* p=f+6;uint16_t n=gnssbin::u16(f+4);uint64_t now=time_us_64();s.lastSentenceUs=now;
   if(f[2]==0x0a&&f[3]==0x04&&n>=40){
    for(unsigned i=40;i+30<=n;i+=30){char ext[31]{};memcpy(ext,p+i,30);if(!strncmp(ext,"MOD=",4)){strncpy(s.model,ext+4,sizeof(s.model)-1);s.f9p=strstr(ext,"F9P")!=nullptr;}}
   }
   if(f[2]==6&&f[3]==0x8b&&n>=4){unsigned matched=0;for(unsigned i=4;i+4<n;){uint32_t key=gnssbin::u32(p+i);i+=4;unsigned type=key>>28,size=type==1||type==2?1:type==3?2:type==4?4:type==5?8:0;if(!size||i+size>n)break;uint32_t value=size==1?p[i]:size==4?gnssbin::u32(p+i):0;if(key==0x40520001&&value==230400)matched|=1;if(key==0x209102a5&&value==1)matched|=2;if(key==0x20910232&&value==1)matched|=4;if(key==0x20910007&&value==1)matched|=8;if(key==0x10740001&&value==1)matched|=16;if(key==0x10730004&&value==1)matched|=32;if(key==0x20910269&&value==1)matched|=64;i+=size;}if(matched)s.configVerified=matched==127;if(s.signalSettings.read(p,n,now))s.l5Enable.readback(s.signalSettings);}
   if(f[2]==5&&n==2&&p[0]==6&&p[1]==0x8a){if(f[3]==1)s.configAck=true;else if(f[3]==0)++s.configNak;if(f[3]==0||f[3]==1){bool pending=s.l5Enable.waitingAck;s.l5Enable.ack(f[3]==1,millis());if(pending)lastSettingsPoll=0;}}
   if(f[2]==1&&f[3]==7&&n==92){s.pvtUs=now;s.positionValid=(p[21]&1)&&p[20]>=3;s.carrier=s.positionValid?((p[21]>>6)&3):0;s.latE7=int32_t(gnssbin::u32(p+28));s.lonE7=int32_t(gnssbin::u32(p+24));s.heightMm=int32_t(gnssbin::u32(p+32));s.hAccMm=gnssbin::u32(p+40);}
   if(f[2]==2&&f[3]==0x15&&n>=16&&n==16+32*p[11]){++s.rawx;s.rawxUs=now;}
   if(f[2]==2&&f[3]==0x13&&n>=8&&n==8+4*p[4]){++s.sfrbx;s.sfrbxUs=now;}
   if(f[2]==1&&f[3]==0x43)s.signals.read(p,n,now);
   if(f[2]==2&&f[3]==0x32)s.corrections.read(p,n,now);
   if(gnssRecording.load() && ((f[2]==2&&(f[3]==0x15||f[3]==0x13||f[3]==0x32))||(f[2]==1&&(f[3]==7||f[3]==0x43))||(f[2]==6&&f[3]==0x8b)||(f[2]==0x0a&&f[3]==4))){logFrame.receivedUs=now;logFrame.bytes=ubx.bytes;memcpy(logFrame.data,f,ubx.bytes);if(!gnssFrames.push(logFrame)){++gnssLogDropped;if(f[2]==2&&f[3]==0x15)++gnssRawxDropped;else if(f[2]==2&&f[3]==0x13)++gnssSfrbxDropped;else ++gnssDiagnosticDropped;}}
  }
  if(binary)continue;
  if(c=='$'){nmeaUsed=0;nmeaLine[nmeaUsed++]=c;}
  else if(nmeaUsed && c!='\r' && c!='\n'){if(nmeaUsed+1<sizeof(nmeaLine))nmeaLine[nmeaUsed++]=c;else nmeaUsed=0;}
  if(parser.push(char(c),nav)){s.nav=nav;s.receivedUs=time_us_64();clock.accept(nav,s.receivedUs);}
  if(parser.packets!=before){s.lastSentenceUs=time_us_64();if(nmeaUsed>7 && !strncmp(nmeaLine+3,"GGA,",4)){nmeaLine[nmeaUsed]=0;strncpy(s.gga,nmeaLine,sizeof(s.gga)-1);s.ggaUs=s.lastSentenceUs;} }
  if(c=='\n')nmeaUsed=0;
 }
 s.sampledUs=time_us_64();s.ubxErrors=ubx.errors;
 bool signalChangeBlocked=s.l5Enable.blocked(millis());
 // Only repair positively read-back disabled GPS L5, before the first correction.
 // RAM only; preserve health policy, all other bands, flash and BBR.
 if(!haveCorr && s.f9p && s.configVerified && correctionTxFrames.load()==0 && s.l5Enable.needed(s.signalSettings)){
  uint8_t enable[9]={0,1,0,0,4,0,0x31,0x10,1};
  if(gnssSend(6,0x8a,enable,sizeof(enable))){s.l5Enable.sent(millis());signalChangeBlocked=true;}
 }
 // Configure only positively identified F9P. RAM layer only: preserve receiver flash/BBR.
 if(!signalChangeBlocked && !haveCorr && s.lastSentenceUs && !s.model[0] && s.sampledUs-lastMonPoll>2000000){if(gnssSend(0x0a,4))lastMonPoll=s.sampledUs;}
 if(!signalChangeBlocked && !haveCorr && s.f9p && configStage==0){
  uint8_t payload[96]={0,1,0,0};unsigned used=4;
  auto key=[&](uint32_t k,uint32_t value,unsigned bytes){for(unsigned i=0;i<4;i++)payload[used++]=k>>(8*i);for(unsigned i=0;i<bytes;i++)payload[used++]=value>>(8*i);};
  key(0x40520001,230400,4);key(0x10730004,1,1);key(0x10740001,1,1);
  key(0x209102a5,1,1);key(0x20910232,1,1);key(0x20910007,1,1);key(0x20910269,1,1);
  if(gnssSend(6,0x8a,payload,used)){configStage=1;configAt=millis();}
 }
 if(configStage==1 && uint32_t(millis()-configAt)>=150){Serial2.end();Serial2.begin(230400);s.baud=230400;lastSwitch=millis();configStage=2;ubx.reset();parser.reset();clock.invalidate();}
 if(!signalChangeBlocked && !haveCorr && configStage>=2 && !s.configVerified && uint32_t(millis()-configAt)>=1000){uint8_t query[32]={0,0,0,0};const uint32_t keys[]={0x40520001,0x209102a5,0x20910232,0x20910007,0x10740001,0x10730004,0x20910269};for(unsigned k=0;k<7;k++)for(unsigned j=0;j<4;j++)query[4+k*4+j]=keys[k]>>(j*8);if(gnssSend(6,0x8b,query,sizeof(query)))configAt=millis();}
 // Poll low-rate diagnostics without changing signal selection (which resets GNSS).
 if(!signalChangeBlocked && !haveCorr && s.f9p && s.configVerified && gnssCommandSent==gnssCommandUsed){
  if(!lastSettingsPoll||uint32_t(millis()-lastSettingsPoll)>=30000){uint8_t q[32]={0,0,0,0};for(unsigned k=0;k<7;k++)for(unsigned j=0;j<4;j++)q[4+k*4+j]=gnssdiag::keys[k]>>(8*j);if(gnssSend(6,0x8b,q,sizeof(q)))lastSettingsPoll=millis();}
  else if(!lastSignalPoll||uint32_t(millis()-lastSignalPoll)>=5000){if(gnssSend(1,0x43))lastSignalPoll=millis();}
 }
 if(!signalChangeBlocked && gnssCommandSent==gnssCommandUsed && !haveCorr && correctionIn.pop(corr)){if(corr.generation==correctionGeneration.load()){haveCorr=true;corrOffset=0;}else ++correctionTxDropped;}
 if(haveCorr){for(unsigned budget=0;budget<32&&corrOffset<corr.bytes&&uart_is_writable(uart1);budget++)Serial2.write(corr.data[corrOffset++]);if(corrOffset==corr.bytes){haveCorr=false;++correctionTxFrames;}}
s.packets=parser.packets;s.errors=parser.errors;s.periodUs=clock.periodUs;s.uartAgeUs=clock.uartAgeUs;s.rejected=clock.rejected;
 s.synchronized=clock.locked(s.sampledUs);s.utcUs=clock.utc(s.sampledUs);s.labelledPpsUs=clock.labelledMono();s.labelledUtcUs=clock.labelledUtc();
 if((!s.lastSentenceUs || s.sampledUs-s.lastSentenceUs>3000000) && uint32_t(millis()-lastSwitch)>=GNSS_AUTOBAUD_MS){
  lastSwitch=millis();baudIndex=(baudIndex+1)%5;s.baud=bauds[baudIndex];configStage=0;s.configAck=false;s.configVerified=false;s.signalSettings={};s.signals={};s.corrections={};lastSignalPoll=lastSettingsPoll=0;Serial2.end();Serial2.begin(s.baud);parser.reset();clock.invalidate();s.synchronized=false;
 }
 mutex_enter_blocking(&stateMutex);sharedGnss=s;mutex_exit(&stateMutex);
}
GnssStatus gnssSnapshot(){mutex_enter_blocking(&stateMutex);auto s=sharedGnss;mutex_exit(&stateMutex);return s;}
bool gnssLocked(const GnssStatus& s,uint64_t now){return s.synchronized&&now>=s.sampledUs&&now-s.sampledUs<100000&&now>=s.ppsUs&&now-s.ppsUs<1500000&&now>=s.receivedUs&&now-s.receivedUs<1500000;}
