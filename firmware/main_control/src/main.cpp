#include <Arduino.h>
#include <SPI.h>
#include <SdFat.h>
#include <Wire.h>
#include <pico/time.h>
#include <pico/mutex.h>
#include <pico/rand.h>
#include <hardware/sync.h>
#include <hardware/pio.h>
#include <hardware/pio_instructions.h>
#include <hardware/clocks.h>
#include <hardware/pwm.h>
#include <hardware/dma.h>
#include <hardware/uart.h>
#include <atomic>
#include "pins.h"
#include "protocol.h"
#include "control.h"
#include "i2c_devices.h"
#include "utc_clock.h"
#include <hardware/watchdog.h>
#ifdef BOARD_PARENT
#include <WiFi.h>
#include "web_page.h"
#else
#include <Adafruit_BNO08x.h>
#endif
bool core1_separate_stack=true;
mutex_t stateMutex;
std::atomic<bool> ready{false},powered{false},runRanging{false};
std::atomic<uint32_t> rows{0},durableRows{0},sdErrors{0},dropped{0};
std::atomic<bool> sdHealthy{false},resetBno{false},captureEnabled{true},pauseAck{false};
bool stopRequested=false;
std::atomic<uint32_t> core1Heartbeat{0};
std::atomic<int> bnoStage{0};
std::atomic<uint32_t> imuRateHz{IMU_HZ};
uint32_t imuPeriod(){return 1000000/imuRateHz.load();}
aq::Ring<aq::Packet,4> controlIn,controlOut;
aq::Health sharedHealth{},remoteHealth{};
aq::Ring<aq::Sample,512> samples;
struct RawImu {uint64_t acquiredUs,sourceUs;uint32_t sequence;uint8_t kind;float x,y,z,w;uint64_t utcUs=0;uint8_t utcSource=0;};
aq::Ring<RawImu,1024> rawImu;
std::atomic<uint32_t> rawRows{0},rawDropped{0};
aq::Ring<aq::Packet,16> events;
uint32_t sessionId=0,linkRx=0,linkErrors=0,remoteSeenMs=0;
int64_t syncOffset=0;uint32_t syncUncertainty=0;
bool syncOk=false;
uint32_t shotSeq=0;int64_t lastTof=0;uint32_t rangeState=0; // 0 waiting, 1 no detection, 2 early, 3 candidate (uncalibrated), 4 unsynced
uint64_t lastTx=0,lastRx=0;
const char* role=
#ifdef BOARD_PARENT
"P_MAIN";
#else
"C_MAIN";
#endif
void setHealth(const aq::Health& h){mutex_enter_blocking(&stateMutex);
 auto v=h;
#ifndef BOARD_PARENT
 v.magX=sharedHealth.magX;v.magY=sharedHealth.magY;v.magZ=sharedHealth.magZ;
 v.flags=(v.flags&~aq::MAG)|(sharedHealth.flags&aq::MAG);
#endif
 v.imuSamples=sharedHealth.imuSamples;v.imuGaps=sharedHealth.imuGaps;v.resets=sharedHealth.resets;v.maxLatenessUs=sharedHealth.maxLatenessUs;v.imuAgeUs=sharedHealth.imuAgeUs;v.rxEdges=sharedHealth.rxEdges;v.flags=(v.flags&~aq::IMU)|(sharedHealth.flags&aq::IMU);sharedHealth=v;mutex_exit(&stateMutex);}
aq::Health health(){mutex_enter_blocking(&stateMutex);auto h=sharedHealth;mutex_exit(&stateMutex);return h;}

#ifdef BOARD_PARENT
#include "spresense_link.h"
#include "phone_reference.h"
phone::Reference phoneReference;
#include "sd_recovery.h"
SdRecovery sdRecovery;
const char* sdLastFailure="NONE";
#endif

// UTC anchors are independent of monotonic ranging and acquisition timestamps.
utcclock::Anchor selectedUtc, receivedUtc;
std::atomic<uint32_t> utcPackets{0};uint32_t rtcWrites=0,rtcWriteErrors=0;
uint64_t rtcLastWrite=0,rtcLastAttempt=0;
std::atomic<uint32_t> gpsTestUntilMs{0},utcTransferTestUntilMs{0};
bool testActive(uint32_t until){return until && int32_t(until-millis())>0;}
utcclock::Source rtcSavedSource=utcclock::NONE;
utcclock::Anchor utcSnapshot(){mutex_enter_blocking(&stateMutex);auto t=selectedUtc;mutex_exit(&stateMutex);return t;}
void utcTick(){
 uint64_t now=time_us_64();const auto& r=i2c_readings();
 utcclock::Anchor rtc;
 if(r.rtc_ok && r.rtc_mono_us)rtc={r.rtc_mono_us,r.rtc_epoch_us,r.rtc_mono_us+2500000,utcclock::RTC};
 utcclock::Anchor chosen;
#ifdef BOARD_PARENT
 auto g=spresenseSnapshot();utcclock::Anchor gps,phone;
 if(!testActive(gpsTestUntilMs.load()) && spresenseLocked(g,now))gps={g.sampledUs,g.utcUs,min(g.ppsUs,g.receivedUs)+1500000,utcclock::GPS};
 if(phoneReference.timeFresh(now/1000))phone={phoneReference.anchorMs*1000,phoneReference.epochMs*1000,phoneReference.anchorMs*1000+180000000,utcclock::PHONE};
 chosen=utcclock::select(gps,phone,rtc,now);
#else
 mutex_enter_blocking(&stateMutex);auto parent=receivedUtc;bool linked=syncOk;mutex_exit(&stateMutex);
 chosen=linked && parent.valid(now)?parent:rtc;
#endif
 mutex_enter_blocking(&stateMutex);selectedUtc=chosen;mutex_exit(&stateMutex);
 bool external=chosen.source==utcclock::GPS || chosen.source==utcclock::PHONE;
#ifndef BOARD_PARENT
 external=external || (chosen.source==utcclock::RTC && !r.rtc_ok && parent.valid(now));
#endif
 // RTC has whole seconds. Readback verifies storage; it does not verify absolute accuracy.
 if(external && chosen.valid(now) && (!rtcLastWrite || chosen.source!=rtcSavedSource || now-rtcLastWrite>=600000000ULL) && (!rtcLastAttempt || now-rtcLastAttempt>=5000000)){
  rtcLastAttempt=now;
  if(i2c_rtc_store(chosen.at(now))){rtcLastWrite=now;rtcSavedSource=chosen.source;++rtcWrites;}else ++rtcWriteErrors;
 }
}

// Core 0 exclusively owns SD, I2C and Wi-Fi. Core 1 owns BNO, tether and shot timing.
SdFs sd;FsFile logFile,slowFile,dumpFile,rawFile;
constexpr size_t LOG_NAME_CAP=64;
char rawName[LOG_NAME_CAP]="";
bool dumping=false;
char logName[LOG_NAME_CAP]="",slowName[LOG_NAME_CAP]="";
const char* sdReason="NOT_STARTED";
uint32_t maxWriteUs=0,lastSyncMs=0;bool stopped=false;
void failSd(const char* reason){
#ifdef BOARD_PARENT
 sdLastFailure=reason;sdRecovery.failed(millis());
#endif
sdHealthy=false;sdErrors++;sdReason=reason;if(logFile)logFile.close();if(slowFile)slowFile.close();if(rawFile)rawFile.close();}
bool openUnique(FsFile& f,char* name,char prefix,const char* header){
 for(unsigned i=0;i<100000;i++){
  const char* label=prefix=='C'?"C_MAIN_IMU":prefix=='I'?"C_MAIN_BNO_EVENTS":prefix=='D'?"C_MAIN_HEALTH_RANGE":prefix=='Q'?"P_MAIN_HEALTH_RANGE":"P_MAIN_TIME_POSITION";
  if(prefix=='C')snprintf(name,LOG_NAME_CAP,"%s_%dHz_%05u.csv",label,int(imuRateHz.load()),i);
  else snprintf(name,LOG_NAME_CAP,"%s_%05u.csv",label,i);
  if(sd.exists(name))continue;
  f=sd.open(name,O_WRONLY|O_CREAT|O_EXCL);
  return f && f.print(header)==strlen(header) && f.sync();
 }return false;
}
bool mountSd(){
 if(!powered || dumping || stopRequested)return false;
 if(sdHealthy)return true;
 if(logFile)logFile.close();if(slowFile)slowFile.close();if(rawFile)rawFile.close();sdHealthy=false;sd.end();
 SPI1.setSCK(PIN_SD_SCK);SPI1.setTX(PIN_SD_MOSI);SPI1.setRX(PIN_SD_MISO);
 if(!sd.begin(SdSpiConfig(PIN_SD_CS,SHARED_SPI,SD_SCK_MHZ(4),&SPI1))){failSd("MOUNT_FAILED");return false;}
 // Verify a NEW file with readback; preserve all existing files and cards.
 char name[16];FsFile check;
 for(unsigned i=0;i<100000;i++){snprintf(name,sizeof(name),"V%05u.BIN",i);if(!sd.exists(name)){check=sd.open(name,O_RDWR|O_CREAT|O_EXCL);break;}}
 uint8_t a[512],b[512];for(unsigned i=0;i<512;i++)a[i]=uint8_t(i*37+sessionId);
 if(!check || check.write(a,512)!=512 || !check.sync() || !check.seekSet(0) || check.read(b,512)!=512 || memcmp(a,b,512)){if(check)check.close();failSd("READBACK_FAILED");return false;}check.close();
 if(!openUnique(logFile,logName,
#ifdef BOARD_PARENT
 'P',
 "mono_us,utc_reference_ms,time_source,phone_age_ms,phone_rtt_ms,phone_position_fresh,phone_latitude,phone_longitude,phone_accuracy_m,crc32\n") ||
#else
 'C',
 "index,scheduled_us,acquired_us,source_us,fresh_mask,qw,qx,qy,qz,ax,ay,az,gx,gy,gz,mx,my,mz,utc_us,utc_source,crc32\n") ||
#endif
 !openUnique(slowFile,slowName,
#ifdef BOARD_PARENT
 'Q',
#else
 'D',
#endif
 "mono_us,flags,bus_v,current_ma,temp_c,pressure_mbar,mag_x,mag_y,mag_z,rows,durable_rows,sd_errors,dropped,link_rx,sync_ok,offset_us,uncertainty_us,shot_seq,tx_us,rx_us,tof_us,range_state,leak,env_mv,buttons_mv,rtc_time,rtc_temp_c,utc_anchor_mono_us,utc_anchor_epoch_us,utc_valid_until_us,utc_source,crc32\n")){failSd("CREATE_FAILED");return false;}

#ifndef BOARD_PARENT
 if(!openUnique(rawFile,rawName,'I',"acquired_us,source_us,sensor_id,sequence,x,y,z,w,utc_us,utc_source,crc32\n")){failSd("RAW_CREATE_FAILED");return false;}
#endif
 #ifdef BOARD_PARENT
 sdRecovery.mounted(millis());
#endif
 rawRows=0;rows=0;durableRows=0;sdReason="NONE";sdHealthy=true;stopped=false;captureEnabled=true;pauseAck=false;lastSyncMs=millis();return true;
}
bool startRecording(){
#ifdef BOARD_PARENT
 sdRecovery.manualStart();
#endif
 return mountSd();
}
bool writeChecked(FsFile& f,char* b,size_t cap,int n){
 if(n<0 || size_t(n)+10>=cap){failSd("ROW_OVERFLOW");return false;}
 uint32_t crc=aq::crc32(b,n);int suffix=snprintf(b+n,cap-n,",%08lx\n",(unsigned long)crc);
 uint64_t t=time_us_64();bool ok=f.write(b,n+suffix)==size_t(n+suffix) && !f.getWriteError();
 maxWriteUs=max(maxWriteUs,uint32_t(time_us_64()-t));if(!ok)failSd("WRITE_FAILED");return ok;
}
void sdTick(){
 aq::Sample s;char b[512];
 for(int budget=0;budget<16 && samples.pop(s);budget++){
  if(!sdHealthy){if(!stopped)dropped++;continue;}
  int n=snprintf(b,sizeof(b),"%lu,%llu,%llu,%llu,%lu,%.6f,%.6f,%.6f,%.6f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.4f,%.4f,%.4f,%llu,%s",
  (unsigned long)s.index,(unsigned long long)s.scheduledUs,(unsigned long long)s.acquiredUs,(unsigned long long)s.sourceUs,(unsigned long)s.fresh,
  s.qw,s.qx,s.qy,s.qz,s.ax,s.ay,s.az,s.gx,s.gy,s.gz,s.mx,s.my,s.mz,(unsigned long long)s.utcUs,utcclock::name(utcclock::Source(s.utcSource)));
  if(writeChecked(logFile,b,sizeof(b),n))rows++;
 }
 RawImu raw;
 for(unsigned budget=0;budget<32 && rawImu.pop(raw);budget++){
  if(!sdHealthy){if(!stopped)rawDropped++;continue;}
  int n=snprintf(b,sizeof(b),"%llu,%llu,%u,%lu,%.6f,%.6f,%.6f,%.6f,%llu,%s",(unsigned long long)raw.acquiredUs,(unsigned long long)raw.sourceUs,unsigned(raw.kind),(unsigned long)raw.sequence,raw.x,raw.y,raw.z,raw.w,(unsigned long long)raw.utcUs,utcclock::name(utcclock::Source(raw.utcSource)));
  if(writeChecked(rawFile,b,sizeof(b),n))rawRows++;
 }
 if(sdHealthy && millis()-lastSyncMs>=1000){lastSyncMs=millis();if(!logFile.sync() || !slowFile.sync() || (rawFile && !rawFile.sync()))failSd("SYNC_FAILED");else durableRows=rows.load();}
}
void stopLog(){
#ifdef BOARD_PARENT
 sdRecovery.stop();
#endif
 captureEnabled=false;stopRequested=true;}
void finishStop(){stopped=true;stopRequested=false;if(sdHealthy){if(!logFile.sync() || !slowFile.sync() || (rawFile && !rawFile.sync()))failSd("STOP_SYNC_FAILED");else {durableRows=rows.load();logFile.close();slowFile.close();if(rawFile)rawFile.close();sdHealthy=false;sdReason="STOPPED";}}}
// Fixed, CRC-protected, equal-length packets make serialization delays symmetric.
// Tether synchronization remains in parent monotonic time, independent of GNSS UTC labels.
int tetherDma=-1;aq::Packet txPacket;
bool sendPacket(aq::Packet& p){
 // UART writes/flush block for an entire packet. DMA lets IMU polling continue.
 if(tetherDma<0 || dma_channel_is_busy(tetherDma) || (uart_get_hw(uart0)->fr & UART_UARTFR_BUSY_BITS))return false;
 aq::seal(p);txPacket=p;
 dma_channel_set_read_addr(tetherDma,&txPacket,false);
 dma_channel_set_trans_count(tetherDma,sizeof(txPacket),true);return true;
}
#ifdef BOARD_PARENT
PIO txPio=pio1;int txSm=-1;uint txOffset=0;volatile uint64_t irqTxUs=0;alarm_id_t shotAlarm=0;
int64_t fireShot(alarm_id_t,void*){
 if(runRanging.load() && txSm>=0){irqTxUs=time_us_64();pio_sm_put(txPio,txSm,4);}return 0;
}
void initBurst(){
 // 4 PIO cycles per period, 800 kHz instruction clock: 200 kHz, five waves.
 // pull/out occur only once; pin stays LOW while stalled waiting for next trigger.
 static const uint16_t code[]={
  uint16_t(pio_encode_pull(false,true)),uint16_t(pio_encode_out(pio_x,32)),
  uint16_t(pio_encode_set(pio_pins,1)|pio_encode_delay(1)),
  uint16_t(pio_encode_set(pio_pins,0)),uint16_t(pio_encode_jmp_x_dec(2))};
 static const pio_program prog={code,5,-1};
 txSm=pio_claim_unused_sm(txPio,false);if(txSm<0 || !pio_can_add_program(txPio,&prog)){txSm=-1;return;}
 txOffset=pio_add_program(txPio,&prog);auto c=pio_get_default_sm_config();
 sm_config_set_wrap(&c,txOffset,txOffset+4);sm_config_set_set_pins(&c,PIN_TX_PWM,1);
 sm_config_set_clkdiv(&c,float(clock_get_hz(clk_sys))/800000.0f);
 pio_gpio_init(txPio,PIN_TX_PWM);pio_sm_set_consecutive_pindirs(txPio,txSm,PIN_TX_PWM,1,true);
 pio_sm_init(txPio,txSm,txOffset,&c);pio_sm_set_pins_with_mask(txPio,txSm,0,1u<<PIN_TX_PWM);pio_sm_set_enabled(txPio,txSm,true);
}
#else
Adafruit_BNO08x bno(PIN_BNO_RST);bool bnoOk=false;
aq::Sample currentSample{};uint32_t imuSamples=0,imuGaps=0,bnoResets=0,maxLatenessUs=0;
uint64_t lastQuatUs=0,lastAccUs=0,lastGyroUs=0,lastMagUs=0,nextSampleUs=0;
uint32_t freshMask=0;
struct Edge{uint64_t us;bool high;};
aq::Ring<Edge,1024> edgeRing;std::atomic<uint32_t> edgeDrops{0},edgeCount{0};
void onComparator(){Edge e{time_us_64(),bool(gpio_get(PIN_RX_COMP))};edgeCount++;if(!edgeRing.push(e))edgeDrops++;}
void initReceiver(){
 pinMode(PIN_LEAK,INPUT);pinMode(PIN_RX_COMP,INPUT);
 gpio_set_function(PIN_RX_THRESHOLD,GPIO_FUNC_PWM);auto slice=pwm_gpio_to_slice_num(PIN_RX_THRESHOLD);
 uint32_t count=clock_get_hz(clk_sys)/125000;auto c=pwm_get_default_config();pwm_config_set_wrap(&c,count-1);
 pwm_init(slice,&c,true);pwm_set_gpio_level(PIN_RX_THRESHOLD,(count*300+1650)/3300);
 attachInterrupt(digitalPinToInterrupt(PIN_RX_COMP),onComparator,CHANGE);
}
bool imuHealthy(uint64_t now){return bnoOk && lastQuatUs && lastAccUs && lastGyroUs && now-lastQuatUs<3*imuPeriod() && now-lastAccUs<3*imuPeriod() && now-lastGyroUs<3*imuPeriod();}
void bnoEvent(void*,sh2_SensorEvent_t*);
bool enableBno(){return bno.enableReport(SH2_GAME_ROTATION_VECTOR,imuPeriod()) && bno.enableReport(SH2_ACCELEROMETER,imuPeriod()) && bno.enableReport(SH2_GYROSCOPE_CALIBRATED,imuPeriod()) && bno.enableReport(SH2_MAGNETIC_FIELD_CALIBRATED,50000);}
void initBno(){SPI.setRX(PIN_BNO_MISO);SPI.setSCK(PIN_BNO_SCK);SPI.setTX(PIN_BNO_MOSI);bnoStage=1;bnoOk=bno.begin_SPI(PIN_BNO_CS,PIN_BNO_INT,&SPI);bnoStage=bnoOk?2:-1;if(bnoOk){sh2_setSensorCallback(bnoEvent,nullptr);bno.wasReset();bnoOk=enableBno();bnoStage=bnoOk?3:-2;}Serial.printf("# BNO_INIT ok=%u ids=%u INT=%u\n",unsigned(bnoOk),unsigned(bno.prodIds.numEntries),unsigned(digitalRead(PIN_BNO_INT)));nextSampleUs=time_us_64()+imuPeriod();}
void bnoEvent(void*,sh2_SensorEvent_t* event){
 sh2_SensorValue_t v{};if(sh2_decodeSensorEvent(&v,event)!=SH2_OK)return;
 uint64_t t=time_us_64();
 RawImu raw{t,v.timestamp,v.sequence,v.sensorId,0,0,0,0};
 auto u=utcSnapshot();raw.utcUs=u.at(t);raw.utcSource=raw.utcUs?u.source:utcclock::NONE;
 switch(v.sensorId){
 case SH2_GAME_ROTATION_VECTOR:raw.x=v.un.gameRotationVector.i;raw.y=v.un.gameRotationVector.j;raw.z=v.un.gameRotationVector.k;raw.w=v.un.gameRotationVector.real;break;
 case SH2_ACCELEROMETER:raw.x=v.un.accelerometer.x;raw.y=v.un.accelerometer.y;raw.z=v.un.accelerometer.z;break;
 case SH2_GYROSCOPE_CALIBRATED:raw.x=v.un.gyroscope.x;raw.y=v.un.gyroscope.y;raw.z=v.un.gyroscope.z;break;
 case SH2_MAGNETIC_FIELD_CALIBRATED:raw.x=v.un.magneticField.x;raw.y=v.un.magneticField.y;raw.z=v.un.magneticField.z;break;
 default:return;
 }
 if(captureEnabled && !rawImu.push(raw))rawDropped++;
   switch(v.sensorId){
    case SH2_GAME_ROTATION_VECTOR:currentSample.qw=v.un.gameRotationVector.real;currentSample.qx=v.un.gameRotationVector.i;currentSample.qy=v.un.gameRotationVector.j;currentSample.qz=v.un.gameRotationVector.k;lastQuatUs=t;currentSample.sourceUs=v.timestamp;freshMask|=1;imuSamples++;break;
    case SH2_ACCELEROMETER:currentSample.ax=v.un.accelerometer.x;currentSample.ay=v.un.accelerometer.y;currentSample.az=v.un.accelerometer.z;lastAccUs=t;freshMask|=2;break;
    case SH2_GYROSCOPE_CALIBRATED:currentSample.gx=v.un.gyroscope.x;currentSample.gy=v.un.gyroscope.y;currentSample.gz=v.un.gyroscope.z;lastGyroUs=t;freshMask|=4;break;
    case SH2_MAGNETIC_FIELD_CALIBRATED:currentSample.mx=v.un.magneticField.x;currentSample.my=v.un.magneticField.y;currentSample.mz=v.un.magneticField.z;lastMagUs=t;freshMask|=8;break;
   }
}
void imuTick(){
 if(bnoOk){
  if(bno.wasReset()){bnoResets++;bnoOk=enableBno();lastQuatUs=lastAccUs=lastGyroUs=lastMagUs=0;freshMask=0;}
  for(unsigned n=0;n<4 && !digitalRead(PIN_BNO_INT);n++)sh2_service();
 }

 uint64_t now=time_us_64();if(now<nextSampleUs)return;
 uint32_t late=now-nextSampleUs;maxLatenessUs=max(maxLatenessUs,late);
 if(late>=imuPeriod()){uint32_t skip=late/imuPeriod();imuGaps+=skip;currentSample.index+=skip;nextSampleUs+=uint64_t(skip)*imuPeriod();}
 currentSample.scheduledUs=nextSampleUs;currentSample.acquiredUs=now;currentSample.fresh=freshMask;
 auto u=utcSnapshot();currentSample.utcUs=u.at(now);currentSample.utcSource=currentSample.utcUs?u.source:utcclock::NONE;
 if((freshMask&7)!=7)imuGaps++;if(captureEnabled && !samples.push(currentSample))dropped++;
 currentSample.index++;freshMask=0;nextSampleUs+=imuPeriod();
}
#endif
void setup1(){
 while(!ready.load(std::memory_order_acquire))tight_loop_contents();
 while(!powered.load())tight_loop_contents();
 Serial1.setTX(PIN_RS485_TX);Serial1.setRX(PIN_RS485_RX);Serial1.setFIFOSize(512);Serial1.begin(115200);
 tetherDma=dma_claim_unused_channel(false);
 if(tetherDma>=0){auto c=dma_channel_get_default_config(tetherDma);channel_config_set_transfer_data_size(&c,DMA_SIZE_8);channel_config_set_read_increment(&c,true);channel_config_set_write_increment(&c,false);channel_config_set_dreq(&c,uart_get_dreq(uart0,true));dma_channel_configure(tetherDma,&c,&uart_get_hw(uart0)->dr,&txPacket,0,false);}
#ifdef BOARD_PARENT
 initBurst();initSpresense();
#else
 initReceiver();initBno();
#endif
}
void loop1(){
#ifdef BOARD_PARENT
 pollSpresense();
#endif
 static aq::Parser parser;static uint32_t seq=0;static uint64_t nextProbe=0,pendingT1=0,lastLink=0;
 static aq::Sync clock;
#ifdef BOARD_PARENT
 static uint64_t resultDeadline=0,lastUtcSent=0;
#endif
 static uint32_t peerSession=0;
#ifdef BOARD_PARENT
 (void)peerSession;
#endif
#ifndef BOARD_PARENT
 static uint64_t scheduled=0,firstRise=0,confirmed=0,expectedParent=0,replyDue=0;
 static uint32_t expectedSeq=0,baseDrops=0,activeSession=0;
 static aq::Packet pendingReply;
#endif
 static aq::Packet pendingControl;static bool haveControl=false;static uint64_t controlSent=0,controlBegan=0;
 if(!haveControl && controlOut.pop(pendingControl)){haveControl=true;controlSent=0;controlBegan=time_us_64();}
 uint64_t now=time_us_64();aq::Packet p;
 while(Serial1.available())if(parser.push(uint8_t(Serial1.read()),p)){
  if(p.kind==aq::CONTROL || p.kind==aq::CONTROL_REPLY){
#ifdef BOARD_PARENT
   if(p.kind==aq::CONTROL_REPLY && haveControl && p.session==sessionId && p.seq==pendingControl.seq){controlIn.push(p);haveControl=false;}
#else
   if(p.kind==aq::CONTROL && p.session==peerSession)controlIn.push(p);
#endif
   continue;
  }
  uint64_t received=time_us_64();auto local=health();local.rows=rows;local.durableRows=durableRows;local.sdErrors=sdErrors;local.dropped=dropped.load()+rawDropped.load();
#ifndef BOARD_PARENT
  local.imuSamples=imuSamples;local.imuGaps=imuGaps;local.resets=bnoResets;local.maxLatenessUs=maxLatenessUs;
  local.imuAgeUs=lastQuatUs?min(uint64_t(0xffffffff),received-lastQuatUs):0xffffffff;
  if(imuHealthy(received))local.flags|=aq::IMU;else local.flags&=~aq::IMU;
  local.rxEdges=edgeCount;
#endif
  if(sdHealthy)local.flags|=aq::SD;else local.flags&=~aq::SD;
#ifdef BOARD_PARENT
  if(p.kind==aq::REPLY && p.session==sessionId && p.seq==seq && p.t1==pendingT1){
   clock.update(p.t1,p.t2,p.t3,received);lastLink=received;
   aq::Packet cmd;cmd.kind=aq::SCHEDULE;cmd.session=sessionId;cmd.seq=seq;cmd.t1=received+100000;cmd.offset=clock.offset;cmd.uncertainty=clock.uncertainty;
   cmd.health=local;cmd.t3=clock.ok?1:0;cmd.t2=runRanging && clock.ok?1:0;pendingT1=0;
   if(cmd.t2){irqTxUs=0;shotAlarm=add_alarm_at(from_us_since_boot(cmd.t1),fireShot,nullptr,false);if(shotAlarm<=0)cmd.t2=0;}
   sendPacket(cmd);resultDeadline=cmd.t2?cmd.t1+aq::windowUs+50000:0;
   mutex_enter_blocking(&stateMutex);remoteHealth=p.health;remoteSeenMs=millis();linkRx++;syncOk=clock.ok;syncOffset=clock.offset;syncUncertainty=clock.uncertainty;if(cmd.t2){shotSeq=seq;rangeState=0;lastTof=0;lastTx=lastRx=0;}mutex_exit(&stateMutex);
  }else if(p.kind==aq::RESULT && p.session==sessionId && p.seq==seq){
   uint32_t irq=save_and_disable_interrupts();uint64_t tx=irqTxUs;restore_interrupts(irq);
   resultDeadline=0;
   // PIO start estimate includes pull/out latency; absolute electrical calibration is pending.
   int64_t tof=p.t2?int64_t(p.t2)-clock.offset-int64_t(tx):0;
   uint32_t state=!tx || !clock.ok || p.uncertainty==0xffffffff?4:(!p.t2?((p.health.flags&aq::RX_HIGH)?5:1):(tof<int64_t(aq::earlyUs)?2:3));
   mutex_enter_blocking(&stateMutex);lastTx=tx;lastRx=p.t2;lastTof=tof;rangeState=state;shotSeq=p.seq;remoteHealth=p.health;remoteSeenMs=millis();linkRx++;mutex_exit(&stateMutex);
   events.push(p);
  }
#else
  if(p.kind==aq::UTC_ANCHOR){
   if(p.session==peerSession && p.seq==pendingReply.seq && clock.ok){
    auto a=utcclock::transfer(p.t1,p.t2,clock.offset,utcclock::Source(p.t3),received);
    if(a.valid(received)){mutex_enter_blocking(&stateMutex);receivedUtc=a;++utcPackets;mutex_exit(&stateMutex);}
   }
   continue;
  }
  if(p.kind==aq::PROBE){
   if(peerSession!=p.session){peerSession=p.session;scheduled=0;clock.ok=false;mutex_enter_blocking(&stateMutex);receivedUtc={};mutex_exit(&stateMutex);}
   pendingReply={};pendingReply.kind=aq::REPLY;pendingReply.session=p.session;pendingReply.seq=p.seq;pendingReply.t1=p.t1;pendingReply.t2=received;pendingReply.health=local;replyDue=received+3000;
   lastLink=received;
   mutex_enter_blocking(&stateMutex);remoteHealth=p.health;remoteSeenMs=millis();linkRx++;mutex_exit(&stateMutex);
  }else if(p.kind==aq::SCHEDULE && p.session==peerSession && p.seq==pendingReply.seq){
   clock.offset=p.offset;clock.uncertainty=p.uncertainty;clock.ok=p.t3==1 && p.uncertainty<10000;
   mutex_enter_blocking(&stateMutex);syncOk=clock.ok;syncOffset=clock.offset;syncUncertainty=clock.uncertainty;mutex_exit(&stateMutex);
   if(p.t2 && clock.ok){
    int64_t target=int64_t(p.t1)+p.offset;
    if(target>int64_t(received)+1000 && target<int64_t(received)+500000){scheduled=target;expectedParent=p.t1;expectedSeq=p.seq;activeSession=p.session;firstRise=confirmed=0;baseDrops=edgeDrops;}
   }else scheduled=0;
  }else if(p.kind==aq::STOP && p.session==peerSession)scheduled=0;
#endif
 }
 now=time_us_64();
 if(haveControl){
#ifdef BOARD_PARENT
  // Keep control frames outside probe/reply/scheduled-shot traffic.
  if(now-controlBegan>5000000){auto c=aq::unpack(pendingControl);c.error=aq::TIMEOUT;aq::pack(pendingControl,c);controlIn.push(pendingControl);haveControl=false;}
  else if(now+40000<nextProbe && nextProbe-now<700000 && (!controlSent || now-controlSent>250000)){
   if(sendPacket(pendingControl))controlSent=now;
  }
#else
  if(sendPacket(pendingControl))haveControl=false;
#endif
 }
#ifdef BOARD_PARENT
 // Send only in the quiet interval, after relative sync and outside acoustic/control traffic.
 if(!testActive(utcTransferTestUntilMs.load()) && clock.ok && !haveControl && !resultDeadline && nextProbe>now && nextProbe-now>400000 && nextProbe-now<650000 && now-lastUtcSent>700000){
  auto a=utcSnapshot();if(a.valid(now)){aq::Packet u;u.kind=aq::UTC_ANCHOR;u.session=sessionId;u.seq=seq;u.t1=now;u.t2=a.at(now);u.t3=a.source;if(sendPacket(u))lastUtcSent=now;}
 }
 if(resultDeadline && now>resultDeadline){resultDeadline=0;mutex_enter_blocking(&stateMutex);rangeState=4;mutex_exit(&stateMutex);}
 if(now>=nextProbe){nextProbe=now+aq::shotPeriodUs;aq::Packet probe;probe.kind=aq::PROBE;probe.session=sessionId;probe.seq=++seq;irqTxUs=0;probe.health=health();probe.t1=time_us_64();pendingT1=probe.t1;sendPacket(probe);}
#else
 if(replyDue && now>=replyDue){pendingReply.t3=time_us_64();sendPacket(pendingReply);replyDue=0;}
 Edge e;while(edgeRing.pop(e))if(scheduled && e.us>=scheduled && e.us<=scheduled+aq::windowUs){
  if(e.high)firstRise=e.us;else if(firstRise && e.us-firstRise>=6 && !confirmed)confirmed=firstRise;
 }
 if(scheduled && now>scheduled+aq::windowUs){
  aq::Packet result;result.kind=aq::RESULT;result.session=activeSession;result.seq=expectedSeq;result.t1=expectedParent;result.t2=confirmed;result.t3=scheduled;
  result.uncertainty=edgeDrops.load()!=baseDrops?0xffffffff:clock.uncertainty;result.health=health();result.health.rxEdges=edgeCount;
  result.health.imuSamples=imuSamples;result.health.imuGaps=imuGaps;result.health.resets=bnoResets;result.health.maxLatenessUs=maxLatenessUs;result.health.imuAgeUs=lastQuatUs?now-lastQuatUs:0xffffffff;
  if(imuHealthy(now))result.health.flags|=aq::IMU;else result.health.flags&=~aq::IMU;
  mutex_enter_blocking(&stateMutex);shotSeq=expectedSeq;lastTx=expectedParent;lastRx=confirmed;lastTof=confirmed?int64_t(confirmed)-int64_t(scheduled):0;rangeState=confirmed?(lastTof<aq::earlyUs?2:3):1;mutex_exit(&stateMutex);
  sendPacket(result);events.push(result);scheduled=0;
 }
 if(resetBno.exchange(false))initBno();
 imuTick();
 // Polling can produce timestamps newer than the loop-entry time.
 now=time_us_64();
#endif
 if(lastLink && now-lastLink>3000000){mutex_enter_blocking(&stateMutex);syncOk=false;rangeState=4;mutex_exit(&stateMutex);clock.ok=false;}
 mutex_enter_blocking(&stateMutex);linkErrors=parser.errors;
#ifndef BOARD_PARENT
 bool magHealthy=bnoOk && lastMagUs && now-lastMagUs<150000;
 sharedHealth.magX=magHealthy?currentSample.mx:NAN;sharedHealth.magY=magHealthy?currentSample.my:NAN;sharedHealth.magZ=magHealthy?currentSample.mz:NAN;
 if(magHealthy)sharedHealth.flags|=aq::MAG;else sharedHealth.flags&=~aq::MAG;
 sharedHealth.imuSamples=imuSamples;sharedHealth.imuGaps=imuGaps;sharedHealth.resets=bnoResets;sharedHealth.maxLatenessUs=maxLatenessUs;
 sharedHealth.imuAgeUs=lastQuatUs?min(uint64_t(0xffffffff),now-lastQuatUs):0xffffffff;sharedHealth.rxEdges=edgeCount;
 if(imuHealthy(now))sharedHealth.flags|=aq::IMU;else sharedHealth.flags&=~aq::IMU;
#endif
 mutex_exit(&stateMutex);pauseAck=!captureEnabled.load();core1Heartbeat=millis();
}
String int64Text(int64_t v){char b[24];snprintf(b,sizeof(b),"%lld",(long long)v);return String(b);}
String num(float v){return isfinite(v)?String(v,4):String("null");}
String healthJson(const aq::Health& h){
 String s="{\"flags\":"+String(h.flags)+",\"rows\":"+String(h.rows)+",\"durable_rows\":"+String(h.durableRows)+",\"sd_errors\":"+String(h.sdErrors)+",\"dropped\":"+String(h.dropped)+",\"imu_samples\":"+String(h.imuSamples)+",\"imu_gaps\":"+String(h.imuGaps)+",\"imu_age_us\":"+String(h.imuAgeUs)+",\"imu_resets\":"+String(h.resets)+",\"max_lateness_us\":"+String(h.maxLatenessUs)+",\"rx_edges\":"+String(h.rxEdges);
 s+=",\"bus_v\":"+num(h.volts)+",\"current_ma\":"+num(h.currentMa)+",\"temp_c\":"+num(h.temperature)+",\"pressure_mbar\":"+num(h.pressure)+",\"mag_x\":"+num(h.magX)+",\"mag_y\":"+num(h.magY)+",\"mag_z\":"+num(h.magZ)+",\"env_mv\":"+String(h.envMv)+",\"buttons_mv\":"+String(h.buttonsMv)+"}";return s;
}
#ifdef BOARD_PARENT
String spresenseJson(const SpresenseStatus& s,uint64_t now){
 bool fresh=spresenseFresh(s,now),locked=spresenseLocked(s,now);
 String out="{\"link_fresh\":"+String(fresh?"true":"false")+",\"fix_valid\":"+String(fresh && (s.nav.flags&spnav::position_valid)?"true":"false")+",\"time_valid\":"+String(fresh && (s.nav.flags&spnav::time_valid)?"true":"false")+",\"sync_candidate\":"+String(locked?"true":"false");
 out+=",\"pps_fresh\":"+String(s.ppsCount && now>=s.ppsUs && now-s.ppsUs<1500000?"true":"false")+",\"satellites_used\":"+String(s.nav.satellites)+",\"fix\":"+String(s.nav.fix)+",\"pps_count\":"+String(s.ppsCount)+",\"pps_period_us\":"+String(s.periodUs)+",\"packets\":"+String(s.packets)+",\"crc_errors\":"+String(s.crcErrors)+",\"overflows\":"+String(s.overflows)+",\"sync_rejected\":"+String(s.rejected);
 out+=",\"uart_after_pps_us\":"+String(s.uartAgeUs)+",\"nav_utc_s\":"+int64Text(s.nav.utc_s)+",\"nav_usec\":"+String(s.nav.usec)+",\"link_age_ms\":"+(s.packets?int64Text((now-s.receivedUs)/1000):String("null"))+",\"utc_candidate_us\":"+(locked?int64Text(s.utcUs):String("null"))+",\"sd_ok\":"+String(fresh && (s.nav.flags&spnav::sd_ok)?"true":"false")+",\"imu_ok\":"+String(fresh && (s.nav.flags&spnav::imu_ok)?"true":"false")+"}";return out;
}
#endif
#ifdef BOARD_PARENT
struct ReferenceTime {const char* source;uint64_t utcMs;};
ReferenceTime referenceTime(uint64_t now){auto a=utcSnapshot();return {a.valid(now)?utcclock::name(a.source):"none",a.at(now)/1000};}
String phoneJson(uint64_t now){
 auto t=referenceTime(now);auto& p=phoneReference;bool fresh=p.timeFresh(now/1000),position=p.positionFresh(now/1000);
 String s="{\"source\":\""+String(t.source)+"\",\"utc_ms\":"+(t.utcMs?int64Text(t.utcMs):String("null"))+",\"phone_time_fresh\":"+String(fresh?"true":"false")+",\"phone_age_ms\":"+(p.haveTime?int64Text(now/1000-p.anchorMs):String("null"))+",\"rtt_ms\":"+String(p.rttMs)+",\"position_fresh\":"+String(position?"true":"false");
 s+=",\"latitude\":"+(position?String(p.latE7/1e7,7):String("null"))+",\"longitude\":"+(position?String(p.lonE7/1e7,7):String("null"))+",\"accuracy_m\":"+(position?String(p.accuracyMm/1000.0,2):String("null"))+"}";return s;
}
#endif
String utcJson(){auto a=utcSnapshot();uint64_t now=time_us_64();const auto& r=i2c_readings();
 return "{\"valid\":"+String(a.valid(now)?"true":"false")+",\"source\":\""+String(a.valid(now)?utcclock::name(a.source):"none")+"\",\"mono_us\":"+int64Text(now)+",\"utc_us\":"+(a.valid(now)?int64Text(a.at(now)):String("null"))+",\"anchor_mono_us\":"+int64Text(a.mono)+",\"anchor_epoch_us\":"+int64Text(a.epoch)+",\"absolute_accuracy_verified\":false,\"rtc_valid\":"+String(r.rtc_ok?"true":"false")+",\"rtc_time\":\""+String(r.rtc_time)+"\",\"rtc_writes\":"+String(rtcWrites)+",\"rtc_write_errors\":"+String(rtcWriteErrors)+",\"received_anchors\":"+String(utcPackets.load())+"}";
}
String statusJson(){
 mutex_enter_blocking(&stateMutex);auto local=sharedHealth,remote=remoteHealth;uint32_t age=remoteSeenMs?millis()-remoteSeenMs:0xffffffff;bool sync=syncOk;auto uncertainty=syncUncertainty;auto offset=syncOffset;auto lr=linkRx,le=linkErrors,seq=shotSeq,rs=rangeState;auto tof=lastTof;mutex_exit(&stateMutex);
 String out="{\"raw_rows\":"+String(rawRows.load())+",\"raw_dropped\":"+String(rawDropped.load())+",\"raw_file\":\""+String(rawName)+"\",\"role\":\""+String(role)+"\",\"parent_relative_time\":true,\"utc_valid\":"+String(utcSnapshot().valid(time_us_64())?"true":"false")+",\"utc\":"+utcJson()+",\"imu_hz\":"+String(imuRateHz.load())+",\"ranging\":"+String(runRanging?"true":"false")+",\"link_age_ms\":"+String(age)+",\"link_rx\":"+String(lr)+",\"link_errors\":"+String(le)+",\"sync_ok\":"+String(sync?"true":"false")+",\"offset_us\":"+int64Text(offset)+",\"uncertainty_us\":"+String(uncertainty)+",\"shot_seq\":"+String(seq)+",\"range_state\":"+String(rs)+",\"tof_us\":"+int64Text(tof)+",\"range_m\":";
 out+=rs==3?String(tof*aq::soundSpeed/1000000.0,4):String("null");
 #ifdef BOARD_PARENT
 out+=",\"utc_test\":{\"gps_suppressed\":"+String(testActive(gpsTestUntilMs.load())?"true":"false")+",\"transfer_suppressed\":"+String(testActive(utcTransferTestUntilMs.load())?"true":"false")+"}";
 out+=",\"sd_recovery\":{\"enabled\":"+String(sdRecovery.wanted?"true":"false")+",\"waiting\":"+String(sdRecovery.pending?"true":"false")+",\"exhausted\":"+String(sdRecovery.exhausted()?"true":"false")+",\"attempts\":"+String(sdRecovery.attempts)+",\"recoveries\":"+String(sdRecovery.recoveries)+",\"last_failure\":\""+String(sdLastFailure)+"\"}";
 auto sp=spresenseSnapshot();out+=",\"spresense\":"+spresenseJson(sp,time_us_64())+",\"phone\":"+phoneJson(time_us_64());
#endif
 out+=",\"range_calibrated\":false,\"sd_reason\":\""+String(sdReason)+"\",\"log_file\":\""+String(logName)+"\",\"health_file\":\""+String(slowName)+"\",\"max_write_us\":"+String(maxWriteUs)+",\"local\":"+healthJson(local)+",\"remote\":"+healthJson(remote)+"}";return out;
}
#include "rx_monitor.h"
#include "experiment_control.h"
#ifdef BOARD_PARENT
#include "web_control.h"
#endif
void updateSensors(){
 const auto& r=i2c_readings();auto h=health();h.uptimeMs=millis();h.rows=rows;h.durableRows=durableRows;h.sdErrors=sdErrors;h.dropped=dropped.load()+rawDropped.load();
 h.flags&=aq::IMU; if(powered)h.flags|=aq::POWER;if(sdHealthy)h.flags|=aq::SD;
 auto u=utcSnapshot();if(u.valid(time_us_64()))h.flags|=aq::UTC_VALID | (uint16_t(u.source)<<12);if(rtcWrites)h.flags|=aq::RTC_SAVED;
 if(r.ina_ok)h.flags|=aq::INA;if(r.tsys_ok)h.flags|=aq::TEMP;if(r.prs_ok)h.flags|=aq::PRESSURE;if(r.rtc_ok)h.flags|=aq::RTC;if(r.lis_ok)h.flags|=aq::MAG;
 h.volts=r.ina_ok?r.ina_v:NAN;h.currentMa=r.ina_ok?r.ina_ma:NAN;h.temperature=r.tsys_ok?r.tsys_temp:NAN;h.pressure=r.prs_ok?r.prs_mbar:NAN;
 h.magX=r.lis_ok?r.lis_x:NAN;h.magY=r.lis_ok?r.lis_y:NAN;h.magZ=r.lis_ok?r.lis_z:NAN;
 if(!monitorActive)h.buttonsMv=uint32_t(analogRead(PIN_BTN_ADC))*3300/4095;
#ifndef BOARD_PARENT
 // Active LOW, with 100k pull-up and 10nF filter on the board; require 20ms stable wet.
 static uint32_t lowSince=0;
 if(!digitalRead(PIN_LEAK)){if(!lowSince)lowSince=millis();if(millis()-lowSince>=20)h.flags|=aq::LEAK;}else lowSince=0;
 if(!monitorActive)h.envMv=uint32_t(analogRead(PIN_RX_ENV))*3300/4095;
 if(digitalRead(PIN_RX_COMP))h.flags|=aq::RX_HIGH;
#endif
 setHealth(h);
}
void saveHealth(){
#ifdef BOARD_PARENT
 if(sdHealthy){uint64_t now=time_us_64();auto t=referenceTime(now);auto& p=phoneReference;bool pos=p.positionFresh(now/1000);char line[320];
 int n=snprintf(line,sizeof(line),"%llu,%llu,%s,%lld,%lu,%u,%.7f,%.7f,%.2f",(unsigned long long)now,(unsigned long long)t.utcMs,t.source,p.haveTime?(long long)(now/1000-p.anchorMs):-1LL,(unsigned long)p.rttMs,unsigned(pos),pos?p.latE7/1e7:NAN,pos?p.lonE7/1e7:NAN,pos?p.accuracyMm/1000.0:NAN);
 writeChecked(logFile,line,sizeof(line),n);}
#endif
 if(!sdHealthy)return;
 auto h=health();mutex_enter_blocking(&stateMutex);auto lr=linkRx;auto sy=syncOk;auto of=syncOffset;auto un=syncUncertainty;auto ss=shotSeq;auto tx=lastTx,rx=lastRx;auto tf=lastTof;auto rs=rangeState;mutex_exit(&stateMutex);
 const auto& rtc=i2c_readings();auto ua=utcSnapshot();
 char b[768];int n=snprintf(b,sizeof(b),"%llu,%u,%.5f,%.3f,%.5f,%.3f,%.4f,%.4f,%.4f,%lu,%lu,%lu,%lu,%lu,%u,%lld,%lu,%lu,%llu,%llu,%lld,%lu,%u,%u,%u,%s,%.2f,%llu,%llu,%llu,%s",
 (unsigned long long)time_us_64(),h.flags,h.volts,h.currentMa,h.temperature,h.pressure,h.magX,h.magY,h.magZ,
 (unsigned long)h.rows,(unsigned long)h.durableRows,(unsigned long)h.sdErrors,(unsigned long)h.dropped,(unsigned long)lr,unsigned(sy),(long long)of,(unsigned long)un,(unsigned long)ss,(unsigned long long)tx,(unsigned long long)rx,(long long)tf,(unsigned long)rs,unsigned(bool(h.flags&aq::LEAK)),h.envMv,h.buttonsMv,rtc.rtc_time,rtc.rtc_temp,(unsigned long long)ua.mono,(unsigned long long)ua.epoch,(unsigned long long)ua.until,utcclock::name(ua.source));
 if(writeChecked(slowFile,b,sizeof(b),n)){
#ifdef BOARD_PARENT
 rows++;
#endif
 }
}
void setup(){
 Serial.begin(115200);mutex_init(&stateMutex);sessionId=get_rand_32();analogReadResolution(12);
 pinMode(PIN_BTN_ADC,INPUT);pinMode(PIN_LED_STATUS,OUTPUT);
#ifdef BOARD_PARENT
 pinMode(PIN_TX_PWM,OUTPUT);digitalWrite(PIN_TX_PWM,LOW);
 pinMode(PIN_GNSS_PPS,INPUT);pinMode(PIN_SPRESENSE_RX,INPUT);
#endif
 ready.store(true,std::memory_order_release);
 Serial.printf("# AquaBeacon MAIN 0.1 role=%s session=%08lx imu_hz=%d ranging=OFF\n",role,(unsigned long)sessionId,IMU_HZ);
#ifdef BOARD_PARENT
 wifiSetup();
#endif
}
void loop(){
 static uint32_t powerTry=0,lastHealth=0,lastReport=0;
#ifndef BOARD_PARENT
 monitorTick();
#endif
 if(!powered && millis()-powerTry>=1000){
  powerTry=millis();if(uint32_t(analogRead(PIN_BTN_ADC))*3300/4095>=V3D_PRESENT_MIN_MV){
   i2c_init();i2c_sensors_setup();powered=true;pinMode(PIN_SD_CD,INPUT);mountSd();
   Serial.printf("# POWER_READY %s SD=%s\n",role,sdReason);
  }
 }
 controlTick();
#ifdef BOARD_PARENT
 if(sdHealthy)sdRecovery.healthy(millis());
 else if(powered && !stopRequested && !dumping && !localControlBusy && sdRecovery.due(millis())){sdRecovery.attempting(millis());mountSd();}
#endif
 if(powered){i2c_sensors_tick(millis());utcTick();if(millis()-lastHealth>=10){lastHealth=millis();updateSensors();}sdTick();if(stopRequested && pauseAck && samples.head.load()==samples.tail.load() && rawImu.head.load()==rawImu.tail.load())finishStop();}
#ifdef BOARD_PARENT
 webTick();
#endif
 while(Serial.available()){
  char c=Serial.read();
  if(c=='h'||c=='?'){if(!dumping)Serial.println(statusJson());}
  else if(c=='B' && stopped && !dumping){watchdog_reboot(0,0,100);}
  else if(c=='s'){if(!monitorActive)startRecording();}else if(c=='q')stopLog();else if(c=='r')resetBno=true;
#ifndef BOARD_PARENT
 else if(c=='m')monitorStart();
 else if(c=='n')monitorStop();
 else if(c=='v' && !monitorActive)Serial.printf("# RX_DIAG cmp=%u env_adc=%u threshold_mv=300 leak=%u\n",unsigned(digitalRead(PIN_RX_COMP)),unsigned(analogRead(PIN_RX_ENV)),unsigned(digitalRead(PIN_LEAK)));
#endif
else if(c=='d' || c=='e' || c=='i'){if(stopped && !dumping){dumpFile=sd.open(c=='d'?logName:(c=='i'?rawName:slowName),O_RDONLY);if(dumpFile){dumping=true;Serial.println("# DUMP_BEGIN");}}}else if(c=='b')Serial.printf("# BNO_STAGE %d\n",int(bnoStage));
#ifdef BOARD_PARENT
  else if(c=='F'){gpsTestUntilMs=millis()+90000;}
  else if(c=='G'){gpsTestUntilMs=0;utcTransferTestUntilMs=0;}
  else if(c=='J'){utcTransferTestUntilMs=millis()+15000;}
  else if(c=='T'){runRanging=true;Serial.println("# RANGING_ENABLED 1Hz five-wave 200kHz");}
  else if(c=='X'){runRanging=false;Serial.println("# RANGING_DISABLED");}
  else if(c=='w')Serial.printf("# WIFI ssid=AquaBeacon-MAIN password=%s ip=%s\n",apPassword,WiFi.softAPIP().toString().c_str());
#endif
 }
 if(millis()-lastReport>=1000){lastReport=millis();saveHealth();if(!dumping && Serial && Serial.availableForWrite()>0)Serial.println(statusJson());}
 if(dumping){
  // USB may accept fewer bytes (or zero) when its cross-core mutex is busy.
  // Preserve unaccepted bytes; otherwise fast SD readback silently loses chunks.
  static uint8_t buffer[128];static size_t used=0,sent=0;
  if(sent<used){if(Serial.availableForWrite()>0)sent+=Serial.write(buffer+sent,used-sent);}
  else {int got=dumpFile.read(buffer,sizeof(buffer));used=got>0?got:0;sent=0;if(got<=0){dumpFile.close();dumping=false;Serial.println("# DUMP_END");}}
 }
 aq::Packet event;while(events.pop(event)){/* Shot fields persist in the periodic health log. */}
 digitalWrite(PIN_LED_STATUS,(millis()/500)%2);
}
