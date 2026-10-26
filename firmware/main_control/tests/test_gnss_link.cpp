// Run the production pollGnss with fake UART/time; no source-code reimplementation.
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <deque>
#include <vector>
#include "protocol.h"
constexpr int PIN_GNSS_TX=8,PIN_GNSS_RX=9,PIN_GNSS_PPS=6,INPUT=0,RISING=1,uart1=1;
constexpr unsigned GNSS_BAUD_FIRST=38400,GNSS_BAUD_SECOND=9600,GNSS_AUTOBAUD_MS=3000;
int stateMutex;
uint64_t clockUs=1000000;
uint64_t time_us_64(){return clockUs;}
uint32_t millis(){return clockUs/1000;}
void mutex_enter_blocking(int*){} void mutex_exit(int*){}
uint32_t save_and_disable_interrupts(){return 0;}void restore_interrupts(uint32_t){}
bool uart_is_writable(int){return true;}
void pinMode(int,int){}int digitalPinToInterrupt(int x){return x;}void attachInterrupt(int,void(*)(),int){}
struct Uart {
 std::deque<uint8_t> rx;std::vector<uint8_t> tx;
 void setTX(int){}void setRX(int){}void setFIFOSize(int){}void begin(unsigned){}void end(){}
 bool overflow(){return false;}bool available(){return !rx.empty();}
 uint8_t read(){auto c=rx.front();rx.pop_front();return c;}
 void write(uint8_t c){tx.push_back(c);}
} Serial2;
#include "gnss_link.h"
void inject(uint8_t cls,uint8_t id,const std::vector<uint8_t>& p){std::vector<uint8_t> f(p.size()+8);gnssbin::encode(cls,id,p.data(),p.size(),f.data());for(auto c:f)Serial2.rx.push_back(c);}
void pump(unsigned n=30){while(n--){clockUs+=1000;pollGnss();}}
int main(){
 std::vector<uint8_t> mon(70);memcpy(mon.data()+40,"MOD=NEO-F9P",11);inject(10,4,mon);pump();assert(gnssSnapshot().f9p);
 clockUs+=200000;pump();
 // Production readback must remain verified after a different VALGET response.
 std::vector<uint8_t> config{1,0,0,0};
 auto add=[&](uint32_t k,uint32_t val,unsigned size){for(unsigned j=0;j<4;j++)config.push_back(k>>(8*j));for(unsigned j=0;j<size;j++)config.push_back(val>>(8*j));};
 for(auto k:{0x209102a5u,0x20910232u,0x20910007u,0x10740001u,0x10730004u,0x20910269u})add(k,1,1);add(0x40520001,230400,4);
 inject(6,0x8b,config);pump();assert(gnssSnapshot().configVerified);
 config={1,0,0,0};for(unsigned i=0;i<7;i++)add(gnssdiag::keys[i],i==2?0:i==6?3:1,1);
 gnssRecording=true;inject(6,0x8b,config);pump();auto g=gnssSnapshot();assert(g.configVerified&&g.signalSettings.complete()&&g.signalSettings.values[2]==0);
 assert(g.l5Enable.attempted&&g.l5Enable.waitingAck);inject(5,1,{6,0x8a});pump();assert(gnssSnapshot().l5Enable.acknowledged);
 clockUs+=700000;config={1,0,0,0};for(unsigned i=0;i<7;i++)add(gnssdiag::keys[i],i==6?3:1,1);
 inject(6,0x8b,config);pump();assert(gnssSnapshot().l5Enable.verified);
 std::vector<uint8_t> rc{2,4,0,0,0,0,0x4d,4};inject(2,0x32,rc);pump();assert(gnssSnapshot().corrections.used==1);
 std::vector<uint8_t> sig(40);sig[5]=2;sig[8]=sig[24]=2;sig[9]=sig[25]=13;sig[10]=0;sig[26]=4;sig[15]=sig[31]=7;sig[18]=sig[34]=0x90;
 inject(1,0x43,sig);pump();assert(gnssSnapshot().signals.galDualPhase==1);
 GnssFrame f;unsigned logged=0;bool cfg=false,rtcm=false,nav=false;
 while(gnssFrames.pop(f)){++logged;cfg|=f.data[2]==6&&f.data[3]==0x8b;rtcm|=f.data[2]==2&&f.data[3]==0x32;nav|=f.data[2]==1&&f.data[3]==0x43;}
 assert(logged==4&&cfg&&rtcm&&nav&&gnssLogDropped==0);
 // Diagnostic polling and correction forwarding must not interleave inside frames.
 CorrectionFrame corr;corr.bytes=9;corr.generation=correctionGeneration;memset(corr.data,0xd3,corr.bytes);assert(correctionIn.push(corr));pump();assert(correctionTxFrames==1);
 gnssbin::UbxParser parser;bool settingsQuery=false,signalQuery=false;
 for(auto b:Serial2.tx)if(parser.push(b)){settingsQuery|=parser.frame[2]==6&&parser.frame[3]==0x8b&&gnssbin::u16(parser.frame+4)==32;signalQuery|=parser.frame[2]==1&&parser.frame[3]==0x43;}
 assert(settingsQuery&&signalQuery);
 // Autobaud recovery clears stale receiver diagnostics.
 clockUs+=4000000;pump(1);g=gnssSnapshot();assert(!g.signalSettings.known&&!g.signals.receivedUs&&!g.corrections.receivedUs);
}
