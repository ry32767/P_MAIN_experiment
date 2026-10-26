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
#include "ranging.h"
#include "acquisition.h"
#include "binary_log.h"
#include "control.h"
#include "sequence_gap.h"
#include "bno_calibration.h"
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
std::atomic<bool> calibrationActive{false};
#ifndef BOARD_PARENT
struct BnoRequest {uint32_t id;uint8_t op;};
struct BnoReply {uint32_t id;aq::Control control;};
aq::Ring<BnoRequest,4> bnoRequests;
aq::Ring<BnoReply,4> bnoReplies;
#endif
aq::Health sharedHealth{},remoteHealth{};
aq::Ring<aq::Sample,512> samples;
struct RawImu {uint64_t acquiredUs,sourceUs;uint32_t sequence;uint8_t kind;float x,y,z,w;uint64_t utcUs=0;uint8_t utcSource=0;};
aq::Ring<RawImu,1024> rawImu;
std::atomic<uint32_t> rawRows{0},rawDropped{0};
aq::Ring<aq::Packet,16> events;
uint32_t sessionId=0,linkRx=0,linkErrors=0,remoteSeenMs=0;
int64_t syncOffset=0;uint32_t syncUncertainty=0;
bool syncOk=false;
uint32_t shotSeq=0;int64_t lastTof=0;uint32_t rangeState=0; // 0 waiting, 1 no detection, 2 early, 3 candidate (uncalibrated), 4 acquisition error
uint64_t lastTx=0,lastRx=0;
aq::AcquisitionConfig acquisitionConfig{200000,3,30,1,1};
uint8_t lastPatternId=aq::noPattern;
aq::AcquisitionConfig acquisitionSnapshot(){mutex_enter_blocking(&stateMutex);auto c=acquisitionConfig;mutex_exit(&stateMutex);return c;}
std::atomic<uint32_t> eventDropped{0},captureRequestDrops{0},captureEdgeDrops{0};
#ifndef BOARD_PARENT
aq::Ring<aq::CaptureRequest,8> captureRequests;
aq::Ring<aq::CaptureEdge,2048> captureEdges;
static bool multipathActive=false;
void multipathStop(uint8_t reason);
void multipathTick();
#endif
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
#include "gnss_link.h"
bool parentCalibrationUsb(const char* line);
bool spresenseGpsEnabled();
#include "spresense_link.h"
#include "phone_reference.h"
phone::Reference phoneReference;
#include "sd_recovery.h"
SdRecovery sdRecovery;
const char* sdLastFailure="NONE";
#else
#include "sd_recovery.h"
SdRecovery childSdRecovery;
const char* childSdLastFailure="NONE";
#endif

// UTC anchors are independent of monotonic ranging and acquisition timestamps.
utcclock::Anchor selectedUtc, receivedUtc;
std::atomic<uint32_t> utcPackets{0};uint32_t rtcWrites=0,rtcWriteErrors=0;
uint64_t rtcLastWrite=0,rtcLastAttempt=0;
std::atomic<uint32_t> gpsTestUntilMs{0},utcTransferTestUntilMs{0};
bool testActive(uint32_t until){return until && int32_t(until-millis())>0;}
#ifdef BOARD_PARENT
bool spresenseGpsEnabled(){return !testActive(gpsTestUntilMs.load());}
#endif
utcclock::Source rtcSavedSource=utcclock::NONE;
utcclock::Anchor utcSnapshot(){mutex_enter_blocking(&stateMutex);auto t=selectedUtc;mutex_exit(&stateMutex);return t;}
void utcTick(){
 uint64_t now=time_us_64();const auto& r=i2c_readings();
 utcclock::Anchor rtc;
 if(r.rtc_ok && r.rtc_mono_us)rtc={r.rtc_mono_us,r.rtc_epoch_us,r.rtc_mono_us+2500000,utcclock::RTC};
 utcclock::Anchor chosen;
#ifdef BOARD_PARENT
 auto g=gnssSnapshot();utcclock::Anchor gps,phone;
 if(!testActive(gpsTestUntilMs.load()) && gnssLocked(g,now))gps={g.labelledPpsUs,g.labelledUtcUs,min(g.ppsUs,g.receivedUs)+1500000,utcclock::GPS};
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
SdFs sd;FsFile logFile,slowFile,dumpFile,rawFile,observationFile;
constexpr size_t LOG_NAME_CAP=64;
char rawName[LOG_NAME_CAP]="",observationName[LOG_NAME_CAP]="";
bool dumping=false;
char logName[LOG_NAME_CAP]="",slowName[LOG_NAME_CAP]="";
const char* sdReason="NOT_STARTED";
uint32_t maxWriteUs=0,lastSyncMs=0;uint8_t lastSdCode=0,lastSdData=0;bool stopped=false;
void failSd(const char* reason,uint8_t code=0,uint8_t data=0);
#ifdef BOARD_PARENT
#include "ntrip_client.h"
#include "ntrip_settings_store.h"
void logGnssObservation(const GnssFrame&);
#include "gnss_storage.h"
void loadMagCalibration();String magJson();
#endif
bool sdCardPresent(){return digitalRead(PIN_SD_CD)==LOW;}
void failSd(const char* reason,uint8_t code,uint8_t data){
#ifdef BOARD_PARENT
 closeGnssFiles();sdLastFailure=reason;sdRecovery.failed(millis());
#else
 childSdLastFailure=reason;childSdRecovery.failed(millis());
#endif
lastSdCode=code;lastSdData=data;sdHealthy=false;sdErrors++;sdReason=reason;if(logFile)logFile.close();if(slowFile)slowFile.close();if(rawFile)rawFile.close();if(observationFile)observationFile.close();}
bool openUnique(FsFile& f,char* name,char prefix,uint8_t stream){
 for(unsigned i=0;i<100000;i++){
  const char* label=prefix=='C'?"C_MAIN_IMU":prefix=='I'?"C_MAIN_BNO_EVENTS":prefix=='D'?"C_MAIN_HEALTH_RANGE":prefix=='Q'?"P_MAIN_HEALTH_RANGE":prefix=='M'?"C_MAIN_MULTIPATH":"P_MAIN_TIME_POSITION";
  if(prefix=='C')snprintf(name,LOG_NAME_CAP,"%s_%dHz_%05u.bin",label,int(imuRateHz.load()),i);
  else snprintf(name,LOG_NAME_CAP,"%s_%05u.bin",label,i);
  if(sd.exists(name))continue;
  f=sd.open(name,O_WRONLY|O_CREAT|O_EXCL);
  aqbin::FileHeader h;h.stream=stream;h.session=sessionId;h.createdUs=time_us_64();
#ifdef BOARD_PARENT
  h.role=1;
#else
  h.role=2;
#endif
  aqbin::seal(h);return f && f.write(&h,sizeof(h))==sizeof(h) && f.sync();
 }return false;
}
bool writeBinary(FsFile& f,uint16_t kind,uint64_t mono,uint32_t seq,const void* payload,uint16_t size){
 // One bounded SD write per record; no float-to-text conversion on the device.
 uint8_t buffer[2112];if(size>sizeof(buffer)-sizeof(aqbin::RecordHeader)){failSd("RECORD_OVERFLOW");return false;}
 aqbin::RecordHeader h;h.kind=kind;h.bytes=size;h.monoUs=mono;h.sequence=seq;aqbin::seal(h,payload);
 memcpy(buffer,&h,sizeof(h));memcpy(buffer+sizeof(h),payload,size);
 uint64_t t=time_us_64();bool ok=f && f.write(buffer,sizeof(h)+size)==sizeof(h)+size && !f.getWriteError();
 maxWriteUs=max(maxWriteUs,uint32_t(time_us_64()-t));if(!ok)failSd("WRITE_FAILED",sd.sdErrorCode(),sd.sdErrorData());return ok;
}
template<class T> bool writeBinary(FsFile& f,uint16_t kind,uint64_t mono,uint32_t seq,const T& payload){return writeBinary(f,kind,mono,seq,&payload,sizeof(T));}
#ifdef BOARD_PARENT
#include "parent_calibration_store.h"
uint32_t gnssObservationSequence=0;
void logGnssObservation(const GnssFrame& frame){
 if(frame.bytes!=100 || frame.data[2]!=1 || frame.data[3]!=7)return;
 const auto* p=frame.data+6;aqbin::GnssObservation b{};b.itow=gnssbin::u32(p);b.received=frame.receivedUs;
 auto a=utcSnapshot();b.utc=a.at(b.received);b.utcSource=b.utc?a.source:0;b.fix=p[20];b.flags=p[21];b.flags2=p[22];
 b.longitude=int32_t(gnssbin::u32(p+24));b.latitude=int32_t(gnssbin::u32(p+28));b.height=int32_t(gnssbin::u32(p+32));
 b.hAcc=gnssbin::u32(p+40);b.vAcc=gnssbin::u32(p+44);b.vn=int32_t(gnssbin::u32(p+48));b.ve=int32_t(gnssbin::u32(p+52));b.vd=int32_t(gnssbin::u32(p+56));b.sAcc=gnssbin::u32(p+68);b.set=parentSetGeneration;
 writeBinary(slowFile,aqbin::GNSS_OBSERVATION,b.received,++gnssObservationSequence,b);
}
bool bodyRotateMag(const float*,float*);
void logMagObservation(){
 static uint32_t previous=0;const auto& r=i2c_readings();
 if(!r.lis_samples||r.lis_samples==previous)return;previous=r.lis_samples;
 if(!sdHealthy||stopRequested||stopped||dumping)return;
 aqbin::MagObservation b{};b.sequence=r.lis_samples;b.begin=r.lis_begin_us;b.end=r.lis_end_us;auto a=utcSnapshot();
 b.utc=a.at(b.end);b.utcSource=b.utc?a.source:0;b.valid=uint16_t(r.lis_raw_ok&&!r.lis_saturated)|(uint16_t(r.lis_status)<<8);
 b.calibration=i2c_mag_coefficients().enabled?magRecordSequence:0;b.set=parentSetGeneration;
 memcpy(b.raw,r.lis_raw,sizeof(b.raw));b.corrected[0]=r.lis_x;b.corrected[1]=r.lis_y;b.corrected[2]=r.lis_z;
 b.bodyValid=(b.valid&1)&&bodyRotateMag(b.corrected,b.body);
 if(!b.bodyValid&&(b.valid&1)&&parentCalibrationLoaded&&parentCalibration.mounts[1].known){parentcal::rotate(parentCalibration.mounts[1],b.corrected,b.body);b.bodyValid=true;}
 if(!b.bodyValid)for(auto& x:b.body)x=NAN;
 if(writeBinary(slowFile,aqbin::MAG_OBSERVATION,b.end,b.sequence,b))++rawRows;
}
#endif
#include "body_calibration_store.h"
bool mountSd(){
 if(!powered || dumping || stopRequested)return false;
 if(sdHealthy)return true;
 if(logFile)logFile.close();if(slowFile)slowFile.close();if(rawFile)rawFile.close();if(observationFile)observationFile.close();sdHealthy=false;sd.end();
 if(!sdCardPresent()){failSd("CARD_NOT_PRESENT");return false;}
 SPI1.setSCK(PIN_SD_SCK);SPI1.setTX(PIN_SD_MOSI);SPI1.setRX(PIN_SD_MISO);
 if(!sd.begin(SdSpiConfig(PIN_SD_CS,SHARED_SPI,SD_SCK_MHZ(SD_SPI_MHZ),&SPI1))){failSd("MOUNT_FAILED",sd.sdErrorCode(),sd.sdErrorData());return false;}
 // Verify a NEW file with readback; preserve all existing files and cards.
 char name[16];FsFile check;
 for(unsigned i=0;i<100000;i++){snprintf(name,sizeof(name),"V%05u.BIN",i);if(!sd.exists(name)){check=sd.open(name,O_RDWR|O_CREAT|O_EXCL);break;}}
 uint8_t a[512],b[512];for(unsigned i=0;i<512;i++)a[i]=uint8_t(i*37+sessionId);
 if(!check || check.write(a,512)!=512 || !check.sync() || !check.seekSet(0) || check.read(b,512)!=512 || memcmp(a,b,512)){if(check)check.close();failSd("READBACK_FAILED",sd.sdErrorCode(),sd.sdErrorData());return false;}check.close();
 if(!openUnique(logFile,logName,
#ifdef BOARD_PARENT
 'P',aqbin::TIME_POSITION) || !openUnique(slowFile,slowName,'Q',aqbin::HEALTH) || !openGnssFiles()
#else
 'C',aqbin::IMU) || !openUnique(slowFile,slowName,'D',aqbin::HEALTH) ||
 !openUnique(rawFile,rawName,'I',aqbin::BNO) || !openUnique(observationFile,observationName,'M',aqbin::CAPTURE_BEGIN)
#endif
 ){failSd("CREATE_FAILED",sd.sdErrorCode(),sd.sdErrorData());return false;}
 #ifdef BOARD_PARENT
 loadMagCalibration();loadParentCalibration();ntrip::bootSettings();sdRecovery.mounted(millis());gnssRecording=true;
 #else
 childSdRecovery.mounted(millis());
 #endif
 loadBodyCalibration();
 rawRows=0;rows=0;durableRows=0;sdReason="NONE";sdHealthy=true;stopped=false;captureEnabled=true;pauseAck=false;lastSyncMs=millis();
#ifdef BOARD_PARENT
 if(parentCalibrationLoaded && !writeBinary(slowFile,aqbin::CALIBRATION_SET,time_us_64(),parentCalibration.generation,parentCalibration))return false;
#endif
 return true;
}
bool startRecording(){
 bodyCameraReadyAtomic=false;
 if(calibrationActive)return false;
#ifdef BOARD_PARENT
 if(i2c_mag_calibration().active)return false;
#endif
#ifdef BOARD_PARENT
 sdRecovery.manualStart();
#else
 childSdRecovery.manualStart();
#endif
 return mountSd();
}
void sdTick(){
 if(sdHealthy && !sdCardPresent())failSd("CARD_REMOVED");
#ifdef BOARD_PARENT
 gnssStorageTick();
#endif
 aq::Sample s;
 for(int budget=0;budget<16 && samples.pop(s);budget++){
  if(!sdHealthy){if(!stopped)dropped++;continue;}
  aqbin::Imu b{};b.index=s.index;b.fresh=s.fresh;b.scheduled=s.scheduledUs;b.acquired=s.acquiredUs;b.source=s.sourceUs;b.utc=s.utcUs;b.utcSource=s.utcSource;
  float v[]={s.qw,s.qx,s.qy,s.qz,s.ax,s.ay,s.az,s.gx,s.gy,s.gz,s.mx,s.my,s.mz};memcpy(b.values,v,sizeof(v));
  if(s.fresh&4)bodyObserveMotion(s.gx,s.gy,s.gz,s.acquiredUs,s.index);
  if(writeBinary(logFile,aqbin::IMU,s.acquiredUs,s.index,b))rows++;
 }
 RawImu raw;
 for(unsigned budget=0;budget<32 && rawImu.pop(raw);budget++){
  if(!sdHealthy){if(!stopped)rawDropped++;continue;}
  aqbin::Bno b{};b.acquired=raw.acquiredUs;b.source=raw.sourceUs;b.utc=raw.utcUs;b.sequence=raw.sequence;b.sensor=raw.kind;b.utcSource=raw.utcSource;
  b.values[0]=raw.x;b.values[1]=raw.y;b.values[2]=raw.z;b.values[3]=raw.w;
  if(writeBinary(rawFile,aqbin::BNO,raw.acquiredUs,raw.sequence,b))rawRows++;
 }
 aq::Packet p;
 for(unsigned budget=0;budget<4 && events.pop(p);budget++)if(sdHealthy){
  aqbin::Shot b{};b.session=p.session;b.shot=p.seq;b.frequency=p.frequency;b.parentScheduled=p.t1;b.reference=p.t3;b.acoustic=p.t2;b.tof=aq::localTof(p.t3,p.t2);b.state=aq::rangeStatus(p.t3,p.t2,p.uncertainty==0xffffffff,p.health.flags&aq::RX_HIGH);b.observation=p.observation;b.observationSeconds=p.observationSeconds;b.rate=p.rate;b.pattern=p.pattern;b.patternId=p.patternId;b.patternGap1=p.patternGap1;b.patternGap2=p.patternGap2;
  writeBinary(slowFile,aqbin::SHOT,time_us_64(),p.seq,b);
 }
 if(sdHealthy && millis()-lastSyncMs>=1000){lastSyncMs=millis();if(!logFile.sync() || !slowFile.sync() || (rawFile && !rawFile.sync()) || (observationFile && !observationFile.sync())
#ifdef BOARD_PARENT
 || !syncGnssFiles()
#endif
)failSd("SYNC_FAILED",sd.sdErrorCode(),sd.sdErrorData());else durableRows=rows.load();}
}
void stopLog(){
#ifdef BOARD_PARENT
 gnssRecording=false;
#endif
 captureEnabled=false;
#ifndef BOARD_PARENT
 multipathStop(aqbin::STOPPED);
#endif
#ifdef BOARD_PARENT
 sdRecovery.stop();
#else
 childSdRecovery.stop();
#endif
 captureEnabled=false;stopRequested=true;}
void finishStop(){
#ifdef BOARD_PARENT
 lastGnssMetaMs=0;gnssStorageTick();
#endif
 stopped=true;stopRequested=false;if(sdHealthy){if(!logFile.sync() || !slowFile.sync() || (rawFile && !rawFile.sync()) || (observationFile && !observationFile.sync())
#ifdef BOARD_PARENT
 || !syncGnssFiles()
#endif
)failSd("STOP_SYNC_FAILED",sd.sdErrorCode(),sd.sdErrorData());else {durableRows=rows.load();logFile.close();slowFile.close();if(rawFile)rawFile.close();if(observationFile)observationFile.close();
#ifdef BOARD_PARENT
 closeGnssFiles();
#endif
 sdHealthy=false;sdReason="STOPPED";}}}
#ifdef AQ_SD_RECOVERY_TEST
// Diagnostic builds only: sync current logs before entering the real failure
// handler. This tests remount/reopen without removing a card or corrupting data.
void injectSdRecoveryTest(){
 bool ready=sdHealthy && !stopped && !stopRequested && !dumping;
 bool synced=ready && logFile.sync() && slowFile.sync() && (!rawFile || rawFile.sync()) && (!observationFile || observationFile.sync());
#ifdef BOARD_PARENT
 synced=synced && syncGnssFiles();
#endif
 if(!synced){Serial.println("# SD_RECOVERY_TEST refused sync_or_state");return;}
 failSd("TEST_WRITE_INTERRUPTED");
 Serial.println("# SD_RECOVERY_TEST injected synced_before_failure=1");
}
#endif
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
PIO txPio=pio1;int txSm=-1;uint txOffset=0;std::atomic<bool> shotFired{false};alarm_id_t shotAlarm=0;uint32_t burstWords[3]={4};unsigned burstCount=1;
int64_t fireShot(alarm_id_t,void*){
 // Both launches share this callback; physical skew still needs calibration.
 if(runRanging.load() && txSm>=0 && tetherDma>=0 && !dma_channel_is_busy(tetherDma) &&
    !(uart_get_hw(uart0)->fr & UART_UARTFR_BUSY_BITS) && !pio_sm_is_tx_fifo_full(txPio,txSm)){
  uart_get_hw(uart0)->dr=aq::shotMarker;for(unsigned i=0;i<burstCount;i++)pio_sm_put(txPio,txSm,burstWords[i]);shotFired=true;
 }return 0;
}
void initBurst(){
 // 4 PIO cycles per period, 800 kHz instruction clock: 200 kHz, five waves.
 // pull/out occur only once; pin stays LOW while stalled waiting for next trigger.
 static const uint16_t code[]={
  uint16_t(pio_encode_pull(false,true)),uint16_t(pio_encode_out(pio_x,16)),uint16_t(pio_encode_out(pio_y,16)),
  uint16_t(pio_encode_set(pio_pins,1)|pio_encode_delay(1)),
  uint16_t(pio_encode_set(pio_pins,0)),uint16_t(pio_encode_jmp_x_dec(3)),
  uint16_t(pio_encode_jmp_y_dec(6)|pio_encode_delay(3))};
 static const pio_program prog={code,7,-1};
 txSm=pio_claim_unused_sm(txPio,false);if(txSm<0 || !pio_can_add_program(txPio,&prog)){txSm=-1;return;}
 txOffset=pio_add_program(txPio,&prog);auto c=pio_get_default_sm_config();
 sm_config_set_wrap(&c,txOffset,txOffset+6);sm_config_set_out_shift(&c,true,false,32);sm_config_set_set_pins(&c,PIN_TX_PWM,1);
 sm_config_set_clkdiv(&c,float(clock_get_hz(clk_sys))/800000.0f);
 pio_gpio_init(txPio,PIN_TX_PWM);pio_sm_set_consecutive_pindirs(txPio,txSm,PIN_TX_PWM,1,true);
 pio_sm_init(txPio,txSm,txOffset,&c);pio_sm_set_pins_with_mask(txPio,txSm,0,1u<<PIN_TX_PWM);pio_sm_set_enabled(txPio,txSm,true);
}
#else
Adafruit_BNO08x bno(PIN_BNO_RST);bool bnoOk=false;
aq::BnoCalibration calibration;
int calibrationError=0;
void clearCalibration(){calibration.clear();calibrationActive=false;calibrationError=0;}
aq::Sample currentSample{};uint32_t imuSamples=0,imuGaps=0,bnoResets=0,maxLatenessUs=0;
aq::SequenceGapTracker bnoSequences[4];
uint64_t lastQuatUs=0,lastAccUs=0,lastGyroUs=0,lastMagUs=0,nextSampleUs=0;
uint32_t freshMask=0;
struct Edge{uint64_t us;bool high;};
aq::Ring<Edge,1024> edgeRing;std::atomic<uint32_t> edgeDrops{0},edgeCount{0};
volatile bool markerArmed=false;volatile uint64_t markerEdgeUs=0;
void onTetherEdge(){if(markerArmed && !markerEdgeUs)markerEdgeUs=time_us_64();}
void armMarker(bool enabled){
 uint32_t irq=save_and_disable_interrupts();markerArmed=enabled;markerEdgeUs=0;
 gpio_acknowledge_irq(PIN_RS485_RX,GPIO_IRQ_EDGE_FALL);restore_interrupts(irq);
}
uint64_t markerEdge(){uint32_t irq=save_and_disable_interrupts();auto t=markerEdgeUs;restore_interrupts(irq);return t;}
void onComparator(){Edge e{time_us_64(),bool(gpio_get(PIN_RX_COMP))};edgeCount++;if(!edgeRing.push(e))edgeDrops++;}
void initReceiver(){
 pinMode(PIN_LEAK,INPUT);pinMode(PIN_RX_COMP,INPUT);
 gpio_set_function(PIN_RX_THRESHOLD,GPIO_FUNC_PWM);auto slice=pwm_gpio_to_slice_num(PIN_RX_THRESHOLD);
 uint32_t count=clock_get_hz(clk_sys)/125000;auto c=pwm_get_default_config();pwm_config_set_wrap(&c,count-1);
 pwm_init(slice,&c,true);pwm_set_gpio_level(PIN_RX_THRESHOLD,(count*300+1650)/3300);
 attachInterrupt(digitalPinToInterrupt(PIN_RX_COMP),onComparator,CHANGE);
 // Observe RX start edges without changing its UART function or direction.
 attachInterrupt(digitalPinToInterrupt(PIN_RS485_RX),onTetherEdge,FALLING);
}
bool imuHealthy(uint64_t now){return bnoOk && lastQuatUs && lastAccUs && lastGyroUs && now-lastQuatUs<3*imuPeriod() && now-lastAccUs<3*imuPeriod() && now-lastGyroUs<3*imuPeriod();}
void bnoEvent(void*,sh2_SensorEvent_t*);
bool enableBno(){return bno.enableReport(SH2_GAME_ROTATION_VECTOR,imuPeriod()) && bno.enableReport(SH2_ACCELEROMETER,imuPeriod()) && bno.enableReport(SH2_GYROSCOPE_CALIBRATED,imuPeriod()) && bno.enableReport(SH2_MAGNETIC_FIELD_CALIBRATED,50000);}
void initBno(){clearCalibration();SPI.setRX(PIN_BNO_MISO);SPI.setSCK(PIN_BNO_SCK);SPI.setTX(PIN_BNO_MOSI);for(auto& s:bnoSequences)s.reset();bnoStage=1;bnoOk=bno.begin_SPI(PIN_BNO_CS,PIN_BNO_INT,&SPI);bnoStage=bnoOk?2:-1;if(bnoOk){sh2_setSensorCallback(bnoEvent,nullptr);bno.wasReset();bnoOk=enableBno();bnoStage=bnoOk?3:-2;}Serial.printf("# BNO_INIT ok=%u ids=%u INT=%u\n",unsigned(bnoOk),unsigned(bno.prodIds.numEntries),unsigned(digitalRead(PIN_BNO_INT)));nextSampleUs=time_us_64()+imuPeriod();}
void bnoEvent(void*,sh2_SensorEvent_t* event){
 sh2_SensorValue_t v{};if(sh2_decodeSensorEvent(&v,event)!=SH2_OK)return;
 uint64_t t=time_us_64();
 RawImu raw{t,v.timestamp,v.sequence,v.sensorId,0,0,0,0};
 auto u=utcSnapshot();raw.utcUs=u.at(t);raw.utcSource=raw.utcUs?u.source:utcclock::NONE;
 int sequenceKind=-1;
 switch(v.sensorId){
 case SH2_GAME_ROTATION_VECTOR:sequenceKind=0;raw.x=v.un.gameRotationVector.i;raw.y=v.un.gameRotationVector.j;raw.z=v.un.gameRotationVector.k;raw.w=v.un.gameRotationVector.real;break;
 case SH2_ACCELEROMETER:sequenceKind=1;raw.x=v.un.accelerometer.x;raw.y=v.un.accelerometer.y;raw.z=v.un.accelerometer.z;break;
 case SH2_GYROSCOPE_CALIBRATED:sequenceKind=2;raw.x=v.un.gyroscope.x;raw.y=v.un.gyroscope.y;raw.z=v.un.gyroscope.z;break;
 case SH2_MAGNETIC_FIELD_CALIBRATED:sequenceKind=3;raw.x=v.un.magneticField.x;raw.y=v.un.magneticField.y;raw.z=v.un.magneticField.z;break;
 default:return;
 }
 if(sequenceKind>0)calibration.event(sequenceKind-1,v.status,t);
 imuGaps+=bnoSequences[sequenceKind].update(uint8_t(v.sequence));
 if(captureEnabled && !rawImu.push(raw))rawDropped++;
   switch(v.sensorId){
    case SH2_GAME_ROTATION_VECTOR:currentSample.qw=v.un.gameRotationVector.real;currentSample.qx=v.un.gameRotationVector.i;currentSample.qy=v.un.gameRotationVector.j;currentSample.qz=v.un.gameRotationVector.k;lastQuatUs=t;currentSample.sourceUs=v.timestamp;freshMask|=1;imuSamples++;break;
    case SH2_ACCELEROMETER:currentSample.ax=v.un.accelerometer.x;currentSample.ay=v.un.accelerometer.y;currentSample.az=v.un.accelerometer.z;lastAccUs=t;freshMask|=2;break;
    case SH2_GYROSCOPE_CALIBRATED:currentSample.gx=v.un.gyroscope.x;currentSample.gy=v.un.gyroscope.y;currentSample.gz=v.un.gyroscope.z;lastGyroUs=t;freshMask|=4;break;
    case SH2_MAGNETIC_FIELD_CALIBRATED:currentSample.mx=v.un.magneticField.x;currentSample.my=v.un.magneticField.y;currentSample.mz=v.un.magneticField.z;lastMagUs=t;freshMask|=8;break;
   }
}
bool endCalibration(){
 // Restore the pre-session dynamic calibration mask and normal magnetic report rate.
 int result=sh2_setCalConfig(calibration.previousMask);
 bool reports=bno.enableReport(SH2_MAGNETIC_FIELD_CALIBRATED,50000);
 if(result!=SH2_OK || !reports){calibrationError=result!=SH2_OK?result:-1;return false;}
 calibration.active=false;calibrationActive=false;return true;
}
void calibrationTick(){
 auto now=time_us_64();calibration.tick(now);
 // On timeout, retain the interlock if restoration fails; the user can retry END.
 if(calibration.expired(now) && !calibrationError)endCalibration();
 BnoRequest request;if(!bnoRequests.pop(request))return;
 BnoReply reply{};reply.id=request.id;auto& answer=reply.control;answer.op=request.op;
 if(!bnoOk){answer.error=aq::IMU_ERROR;}
 else if(request.op==aq::CAL_START){
  if(calibration.active){answer.error=aq::BUSY;}
  else {
   uint8_t mask=0;int result=sh2_getCalConfig(&mask);
   if(result==SH2_OK){
    calibration.start(now,mask);calibrationActive=true;calibrationError=0;
    // CEVA factory/user procedure: accel+mag dynamic calibration, stationary gyro.
    result=sh2_setCalConfig(SH2_CAL_ACCEL|SH2_CAL_MAG);
    if(result==SH2_OK && !bno.enableReport(SH2_MAGNETIC_FIELD_CALIBRATED,20000))result=-1;
    if(result!=SH2_OK){endCalibration();calibrationError=result;}
   }
   if(result!=SH2_OK){calibrationError=result;answer.error=aq::IMU_ERROR;}
  }
 }else if(request.op==aq::CAL_SAVE){
  if(!calibration.ready(now)){answer.error=aq::CAL_NOT_READY;}
  else if(calibration.saved){answer.error=aq::BUSY;}
  else {int result=sh2_saveDcdNow();calibrationError=result;if(result==SH2_OK)calibration.saved=true;else answer.error=aq::IMU_ERROR;}
 }else if(request.op==aq::CAL_END){
  if(calibration.active){calibrationError=0;if(!endCalibration())answer.error=aq::IMU_ERROR;}
 }
 now=time_us_64();
 // Unknown/stale accuracy is encoded as -1; never reuse pre-session sensor status.
 int n=snprintf((char*)answer.data,sizeof(answer.data),"{\"active\":%u,\"saved\":%u,\"a\":%d,\"g\":%d,\"m\":%d,\"ready\":%u,\"err\":%d}",
 unsigned(calibration.active),unsigned(calibration.saved),calibration.fresh(0,now)?calibration.accuracy[0]:-1,
 calibration.fresh(1,now)?calibration.accuracy[1]:-1,calibration.fresh(2,now)?calibration.accuracy[2]:-1,
 unsigned(calibration.ready(now)),calibrationError);
 answer.length=n;bnoReplies.push(reply);
}
void imuTick(){
 if(bnoOk){
  if(bno.wasReset()){clearCalibration();bnoResets++;for(auto& s:bnoSequences)s.reset();bnoOk=enableBno();lastQuatUs=lastAccUs=lastGyroUs=lastMagUs=0;freshMask=0;}
  for(unsigned n=0;n<4 && !digitalRead(PIN_BNO_INT);n++)sh2_service();
 }

 uint64_t now=time_us_64();if(now<nextSampleUs)return;
 uint32_t late=now-nextSampleUs;maxLatenessUs=max(maxLatenessUs,late);
 if(late>=imuPeriod()){uint32_t skip=late/imuPeriod();currentSample.index+=skip;nextSampleUs+=uint64_t(skip)*imuPeriod();}
 currentSample.scheduledUs=nextSampleUs;currentSample.acquiredUs=now;currentSample.fresh=freshMask;
 auto u=utcSnapshot();currentSample.utcUs=u.at(now);currentSample.utcSource=currentSample.utcUs?u.source:utcclock::NONE;
 if(captureEnabled && !samples.push(currentSample))dropped++;
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
 initBurst();initGnss();initSpresense();
#else
 initReceiver();initBno();
#endif
}
void loop1(){
#ifdef BOARD_PARENT
 pollGnss();pollSpresense();
#endif
 static aq::Parser parser;static uint32_t seq=0;static uint64_t nextProbe=0,pendingT1=0,lastLink=0;
 static aq::Sync clock;
#ifdef BOARD_PARENT
 static aq::Packet reservedShot;
 static uint64_t resultDeadline=0,lastUtcSent=0,acousticQuietUntil=0;static uint32_t shotCount=0;
 static aq::AcquisitionConfig lastConfig;static aq::PatternSequence patternSequence(uint8_t(sessionId%aq::patternCount));
#endif
 static uint32_t peerSession=0;
#ifdef BOARD_PARENT
 (void)peerSession;
#endif
#ifndef BOARD_PARENT
 static uint64_t scheduled=0,armDeadline=0,expectedParent=0,replyDue=0;
 static aq::AcousticWindow acoustic;static aq::AcquisitionConfig expectedConfig;
 static uint32_t expectedSeq=0,baseDrops=0,activeSession=0,expectedFrequency=200000;
 static uint16_t expectedSeconds=3;static bool expectedObservation=false;static uint64_t observationEnd=0;static uint32_t observationShot=0;
 static aq::Packet pendingReply;
#endif
 static aq::Packet pendingControl;static bool haveControl=false;static uint64_t controlSent=0,controlBegan=0;
 if(!haveControl && controlOut.pop(pendingControl)){haveControl=true;controlSent=0;controlBegan=time_us_64();}
 uint64_t now=time_us_64();aq::Packet p;
 while(Serial1.available()){
  uint8_t byte=uint8_t(Serial1.read());
#ifndef BOARD_PARENT
  if(armDeadline && !scheduled && !parser.used){
   auto edge=markerEdge();
   if(byte==aq::shotMarker && edge && edge<armDeadline){scheduled=edge;acoustic.start(edge,expectedConfig);armMarker(false);continue;}
   // Unexpected traffic while waiting cannot become a ranging reference.
   armDeadline=0;armMarker(false);mutex_enter_blocking(&stateMutex);rangeState=4;mutex_exit(&stateMutex);
  }
#endif
  if(!parser.push(byte,p))continue;
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
  if(p.kind==aq::REPLY && pendingT1 && p.session==sessionId && p.seq==seq && p.t1==pendingT1){
   clock.update(p.t1,p.t2,p.t3,received);lastLink=received;
   aq::Packet cmd;cmd.kind=aq::SCHEDULE;cmd.session=sessionId;cmd.seq=seq;cmd.t1=received+100000;cmd.offset=clock.offset;cmd.uncertainty=clock.uncertainty;
   auto config=acquisitionSnapshot();cmd.frequency=config.frequency;cmd.observationSeconds=config.seconds;cmd.observationEvery=config.every;cmd.rate=config.rate;cmd.pattern=config.pattern;config.patternId=config.pattern?patternSequence.cursor:aq::noPattern;cmd.patternId=config.patternId;
   cmd.patternGap1=config.pattern?aq::patternGapUs(config.patternId,0):0;cmd.patternGap2=config.pattern?aq::patternGapUs(config.patternId,1):0;
   if(config.frequency!=lastConfig.frequency || config.seconds!=lastConfig.seconds || config.every!=lastConfig.every || config.rate!=lastConfig.rate || config.pattern!=lastConfig.pattern){shotCount=0;lastConfig=config;}
   cmd.health=local;cmd.t3=clock.ok?1:0;cmd.t2=runRanging && bodyCameraReadyAtomic && (p.health.flags&aq::CAMERA_READY) && !(p.health.flags&aq::CALIBRATING) && txSm>=0 && received>=acousticQuietUntil && (!config.pattern || patternSequence.available(cmd.t1))?1:0;pendingT1=0;
   if(cmd.t2){
    pio_sm_set_clkdiv(txPio,txSm,float(clock_get_hz(clk_sys))/(4.0f*config.frequency));
    burstCount=config.pattern?3:1;burstWords[0]=4;
    if(config.pattern){burstWords[0]|=(uint32_t(cmd.patternGap1)*config.frequency/1000000-1)<<16;burstWords[1]=4|((uint32_t(cmd.patternGap2)*config.frequency/1000000-1)<<16);burstWords[2]=4;}
    // Gain monitoring runs with child SD stopped; do not insert observation pauses there.
    cmd.observation=aq::observeShot(shotCount,config,p.health.flags&aq::SD);
   }
   // Arm the child before reserving the shot. Never fire if the arm frame failed.
   bool sent=sendPacket(cmd);
   if(cmd.t2 && sent){shotFired=false;shotAlarm=add_alarm_at(from_us_since_boot(cmd.t1),fireShot,nullptr,false);if(shotAlarm<=0)cmd.t2=0;}
   else cmd.t2=0;
   resultDeadline=cmd.t2?cmd.t1+aq::windowUs+aq::patternTailUs(config)+50000:0;
   if(cmd.t2){reservedShot=cmd;shotCount++;if(config.pattern)patternSequence.reserve(cmd.t1);}
   if(cmd.t2 && cmd.observation)acousticQuietUntil=cmd.t1+aq::observationHoldUs(config);
   mutex_enter_blocking(&stateMutex);remoteHealth=p.health;remoteSeenMs=millis();linkRx++;syncOk=clock.ok;syncOffset=clock.offset;syncUncertainty=clock.uncertainty;if(cmd.t2){lastPatternId=cmd.patternId;shotSeq=seq;rangeState=0;lastTof=0;lastTx=lastRx=0;}mutex_exit(&stateMutex);
  }else if(p.kind==aq::RESULT && resultDeadline && p.session==sessionId && p.seq==seq){
   bool fired=shotFired.load();
   resultDeadline=0;
   // t2 and t3 were both captured on the child. Relative sync is irrelevant to ToF.
   int64_t tof=aq::localTof(p.t3,p.t2);
   uint32_t state=aq::rangeStatus(p.t3,p.t2,!fired || p.uncertainty==0xffffffff || !aq::shotMetadataMatches(p,reservedShot),p.health.flags&aq::RX_HIGH);
   mutex_enter_blocking(&stateMutex);lastTx=p.t3;lastRx=p.t2;lastTof=tof;rangeState=state;shotSeq=p.seq;remoteHealth=p.health;remoteSeenMs=millis();linkRx++;mutex_exit(&stateMutex);
   if(state==4)p.uncertainty=0xffffffff;
   if(!events.push(p))eventDropped++;
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
   if(peerSession!=p.session){peerSession=p.session;scheduled=armDeadline=0;armMarker(false);clock.ok=false;mutex_enter_blocking(&stateMutex);receivedUtc={};mutex_exit(&stateMutex);}
   pendingReply={};pendingReply.kind=aq::REPLY;pendingReply.session=p.session;pendingReply.seq=p.seq;pendingReply.t1=p.t1;pendingReply.t2=received;pendingReply.health=local;replyDue=received+3000;
   lastLink=received;
   mutex_enter_blocking(&stateMutex);remoteHealth=p.health;remoteSeenMs=millis();linkRx++;mutex_exit(&stateMutex);
  }else if(p.kind==aq::SCHEDULE && p.session==peerSession && p.seq==pendingReply.seq){
   clock.offset=p.offset;clock.uncertainty=p.uncertainty;clock.ok=p.t3==1 && p.uncertainty<10000;
   mutex_enter_blocking(&stateMutex);syncOk=clock.ok;syncOffset=clock.offset;syncUncertainty=clock.uncertainty;mutex_exit(&stateMutex);
   scheduled=armDeadline=0;armMarker(false);
   aq::AcquisitionConfig config{p.frequency,p.observationSeconds,p.observationEvery,p.rate,p.pattern,p.patternId};
   bool validPattern=aq::validConfig(config) && aq::validShotPattern(config,p.patternGap1,p.patternGap2);
   if(validPattern){mutex_enter_blocking(&stateMutex);acquisitionConfig=config;mutex_exit(&stateMutex);}
   if(p.t2 && validPattern && !calibrationActive && !bodyRecordingActive && bodyCameraReadyAtomic){
    expectedConfig=config;expectedFrequency=p.frequency;expectedSeconds=p.observationSeconds;expectedObservation=p.observation;
    if(expectedObservation){aq::CaptureRequest req{};req.action=aq::CAPTURE_ARM;req.session=p.session;req.shot=p.seq;req.frequency=p.frequency;req.seconds=p.observationSeconds;if(!captureRequests.push(req))captureRequestDrops++;}
    expectedParent=p.t1;expectedSeq=p.seq;activeSession=p.session;baseDrops=edgeDrops;
    acoustic.start(0);armDeadline=received+aq::markerTimeoutUs;armMarker(true);
    mutex_enter_blocking(&stateMutex);lastPatternId=p.patternId;shotSeq=expectedSeq;rangeState=0;lastTx=lastRx=0;lastTof=0;mutex_exit(&stateMutex);
   }
   if(p.t2 && !validPattern){mutex_enter_blocking(&stateMutex);rangeState=4;mutex_exit(&stateMutex);}
  }else if(p.kind==aq::STOP && p.session==peerSession){scheduled=armDeadline=0;armMarker(false);}
#endif
 }
 now=time_us_64();
 if(haveControl){
#ifdef BOARD_PARENT
  // Keep control frames outside probe/reply/scheduled-shot traffic.
  if(now-controlBegan>5000000){auto c=aq::unpack(pendingControl);c.error=aq::TIMEOUT;aq::pack(pendingControl,c);controlIn.push(pendingControl);haveControl=false;}
  else if(!resultDeadline && !pendingT1 && (!controlSent || now-controlSent>250000)){
   if(sendPacket(pendingControl))controlSent=now;
  }
#else
  if(!armDeadline && sendPacket(pendingControl))haveControl=false;
#endif
 }
#ifdef BOARD_PARENT
 // Send only in the quiet interval, after relative sync and outside acoustic/control traffic.
 if(!testActive(utcTransferTestUntilMs.load()) && clock.ok && !haveControl && !resultDeadline && !pendingT1 && nextProbe>now && nextProbe-now>40000 && now-lastUtcSent>700000){
  auto a=utcSnapshot();if(a.valid(now)){aq::Packet u;u.kind=aq::UTC_ANCHOR;u.session=sessionId;u.seq=seq;u.t1=now;u.t2=a.at(now);u.t3=a.source;if(sendPacket(u)){lastUtcSent=now;if(nextProbe<now+40000)nextProbe=now+40000;}}
 }
 if(resultDeadline && now>resultDeadline){resultDeadline=0;mutex_enter_blocking(&stateMutex);rangeState=4;mutex_exit(&stateMutex);}
 // A transaction owns the half-duplex bus until RESULT/timeout. Controls pause new shots.
 if(pendingT1 && now-pendingT1>200000)pendingT1=0;
 if(aq::canStartProbe(now,nextProbe,resultDeadline,pendingT1,haveControl)){
  aq::Packet probe;probe.kind=aq::PROBE;probe.session=sessionId;probe.seq=seq+1;probe.health=health();probe.t1=time_us_64();
  if(sendPacket(probe)){seq++;shotFired=false;pendingT1=probe.t1;nextProbe=now+aq::shotIntervalUs(acquisitionSnapshot());}
 }
#else
 if(replyDue && now>=replyDue){pendingReply.t3=time_us_64();if(sendPacket(pendingReply))replyDue=0;}
 // Keep queued acoustic edges until the marker byte is validated (it may arrive later).
 Edge e;if(!armDeadline || scheduled)while(edgeRing.pop(e)){
  bool had=bool(acoustic.confirmed);if(scheduled)acoustic.edge(e.us,e.high);
  if(scheduled && !had && acoustic.confirmed && expectedObservation && captureEnabled){
   aq::CaptureRequest req{};req.action=aq::CAPTURE_TRIGGER;req.session=activeSession;req.shot=expectedSeq;req.frequency=expectedFrequency;req.seconds=expectedSeconds;req.reference=scheduled;req.arrival=acoustic.confirmed;
   if(captureRequests.push(req)){observationEnd=req.arrival+uint64_t(req.seconds)*1000000;observationShot=expectedSeq;
    if(!captureEdges.push({req.arrival,observationShot,1}))captureEdgeDrops++;
   }else captureRequestDrops++;
  }
  if(observationEnd && captureEnabled && e.us<=observationEnd){if(!captureEdges.push({e.us,observationShot,uint8_t(e.high)}))captureEdgeDrops++;}
 }
 if(observationEnd && (now>observationEnd || !captureEnabled))observationEnd=0;
 if(armDeadline && ((scheduled && now>scheduled+aq::windowUs+aq::patternTailUs(expectedConfig)) || now>armDeadline)){
  aq::Packet result;result.kind=aq::RESULT;result.session=activeSession;result.seq=expectedSeq;result.t1=expectedParent;result.t2=acoustic.confirmed;result.t3=scheduled;result.frequency=expectedFrequency;result.observationSeconds=expectedSeconds;result.observation=expectedObservation;result.rate=expectedConfig.rate;result.pattern=expectedConfig.pattern;result.patternId=expectedConfig.patternId;result.patternGap1=expectedConfig.pattern?aq::patternGapUs(expectedConfig.patternId,0):0;result.patternGap2=expectedConfig.pattern?aq::patternGapUs(expectedConfig.patternId,1):0;
  result.uncertainty=!scheduled || acoustic.overflow || edgeDrops.load()!=baseDrops?0xffffffff:0;result.health=health();result.health.rxEdges=edgeCount;
  result.health.imuSamples=imuSamples;result.health.imuGaps=imuGaps;result.health.resets=bnoResets;result.health.maxLatenessUs=maxLatenessUs;result.health.imuAgeUs=lastQuatUs?now-lastQuatUs:0xffffffff;
  if(imuHealthy(now))result.health.flags|=aq::IMU;else result.health.flags&=~aq::IMU;
  mutex_enter_blocking(&stateMutex);shotSeq=expectedSeq;lastTx=scheduled;lastRx=acoustic.confirmed;lastTof=aq::localTof(scheduled,acoustic.confirmed);rangeState=aq::rangeStatus(scheduled,acoustic.confirmed,result.uncertainty==0xffffffff,result.health.flags&aq::RX_HIGH);mutex_exit(&stateMutex);
  if(sendPacket(result)){if(!events.push(result))eventDropped++;scheduled=armDeadline=0;armMarker(false);}
 }
 if(resetBno.exchange(false))initBno();
 imuTick();
 calibrationTick();
 // Polling can produce timestamps newer than the loop-entry time.
 now=time_us_64();
#endif
 if(lastLink && now-lastLink>3000000){mutex_enter_blocking(&stateMutex);syncOk=false;rangeState=4;mutex_exit(&stateMutex);clock.ok=false;}
 mutex_enter_blocking(&stateMutex);linkErrors=parser.errors;
#ifndef BOARD_PARENT
 if(calibrationActive||bodyRecordingActive)sharedHealth.flags|=aq::CALIBRATING;else sharedHealth.flags&=~aq::CALIBRATING;
 if(bodyCameraReadyAtomic)sharedHealth.flags|=aq::CAMERA_READY;else sharedHealth.flags&=~aq::CAMERA_READY;
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
 s+=",\"observation_shot\":"+String(h.observationShot)+",\"observation_samples\":"+String(h.observationSamples)+",\"observation_lost\":"+String(h.observationLost)+",\"observation_skipped\":"+String(h.observationSkipped)+",\"observation_active\":"+String(h.observationActive?"true":"false");
 s+=",\"bus_v\":"+num(h.volts)+",\"current_ma\":"+num(h.currentMa)+",\"temp_c\":"+num(h.temperature)+",\"pressure_mbar\":"+num(h.pressure)+",\"mag_x\":"+num(h.magX)+",\"mag_y\":"+num(h.magY)+",\"mag_z\":"+num(h.magZ)+",\"env_mv\":"+String(h.envMv)+",\"buttons_mv\":"+String(h.buttonsMv)+"}";return s;
}
#ifdef BOARD_PARENT
#include "gnss_diagnostic_json.h"
String gnssJson(const GnssStatus& g,uint64_t now){
 bool fresh=g.lastSentenceUs && now>=g.lastSentenceUs && now-g.lastSentenceUs<3000000;
 bool pvtFresh=g.pvtUs && now>=g.pvtUs && now-g.pvtUs<3000000;
 String s="{\"model\":"+ntrip::jsonString(g.model)+",\"f9p\":"+String(g.f9p?"true":"false")+",\"config_ack\":"+String(g.configAck?"true":"false")+",\"config_verified\":"+String(g.configVerified?"true":"false")+",\"config_nak\":"+String(g.configNak)+",\"rawx\":"+String(g.rawx)+",\"sfrbx\":"+String(g.sfrbx)+",\"ubx_errors\":"+String(g.ubxErrors)+",\"uart_overflows\":"+String(g.uartOverflows)+",\"rtk_solution\":"+ntrip::jsonString(pvtFresh?(g.carrier==2?"fixed":g.carrier==1?"float":"single"):"unknown")+",\"latitude\":"+(pvtFresh&&g.positionValid?String(g.latE7/1e7,7):String("null"))+",\"longitude\":"+(pvtFresh&&g.positionValid?String(g.lonE7/1e7,7):String("null"))+",\"h_accuracy_m\":"+(pvtFresh&&g.positionValid?String(g.hAccMm/1000.0,3):String("null"))+",\"enabled\":true,\"link_fresh\":"+String(fresh?"true":"false")+",\"fix_valid\":"+String(fresh && now>=g.receivedUs && now-g.receivedUs<3000000 && (g.nav.flags&spnav::position_valid)?"true":"false")+",\"time_valid\":"+String(fresh && now-g.receivedUs<1500000 && (g.nav.flags&spnav::time_valid)?"true":"false")+",\"sync_candidate\":"+String(gnssLocked(g,now)?"true":"false");
 s+=gnssDiagnosticJson(g,now);
 s+=",\"pps_fresh\":"+String(g.ppsCount && now>=g.ppsUs && now-g.ppsUs<1500000?"true":"false")+",\"pps_count\":"+String(g.ppsCount)+",\"pps_period_us\":"+String(g.periodUs)+",\"uart_after_pps_us\":"+String(g.uartAgeUs)+",\"satellites_used\":"+String(g.nav.satellites)+",\"packets\":"+String(g.packets)+",\"crc_errors\":"+String(g.errors)+",\"sync_rejected\":"+String(g.rejected)+",\"baud\":"+String(g.baud)+",\"bytes\":"+String(g.bytes)+",\"nav_utc_s\":"+int64Text(g.nav.utc_s)+",\"utc_candidate_us\":"+(gnssLocked(g,now)?int64Text(g.utcUs):String("null"))+",\"absolute_accuracy_verified\":false}";return s;
}
String spresenseJson(const SpresenseStatus& s,uint64_t now){
 bool fresh=spresenseFresh(s,now);
 String out="{\"enabled\":"+String(s.enabled?"true":"false")+",\"link_fresh\":"+String(fresh?"true":"false")+",\"transport\":\"single_wire_gpio\",\"baud\":1250,\"pps_input\":\"D02\",\"pps_connected\":"+String(fresh&&s.ppsFresh?"true":"false")+",\"pps_paired\":"+String(fresh&&s.ppsLocked?"true":"false")+",\"imu_time_ready\":"+String(fresh&&s.imuTimeReady?"true":"false")+",\"pps_count\":"+String(s.ppsCount)+",\"pps_period_us\":"+String(s.ppsPeriodUs)+",\"time_diagnostics\":"+String(s.timeDiagnostics?"true":"false")+",\"absolute_accuracy_verified\":false";
 out+=",\"data_level\":"+String(gpio_get(PIN_SPRESENSE_DATA))+",\"data_output\":"+String(gpio_get_dir(PIN_SPRESENSE_DATA))+",\"transport_busy\":"+String(spresenseWire.busy()?"true":"false")+",\"timer_ticks\":"+String(spresenseTimerTicks.load());
 out+=",\"time_valid\":"+String(fresh&&s.synchronized?"true":"false")+",\"time_source\":"+String(s.timeSource)+",\"recording\":"+String(fresh&&s.recording?"true":"false")+",\"sd_ok\":"+String(fresh&&(s.nav.flags&spnav::sd_ok)?"true":"false")+",\"imu_ok\":"+String(fresh&&(s.nav.flags&spnav::imu_ok)?"true":"false");
 out+=",\"packets\":"+String(s.packets)+",\"crc_errors\":"+String(s.crcErrors)+",\"timeouts\":"+String(s.timeouts)+",\"framing_errors\":"+String(s.framing)+",\"collisions\":"+String(s.collisions)+",\"overflows\":"+String(s.overflows)+",\"rejected\":"+String(s.rejected);
 out+=",\"calibration_mask\":"+String(s.calibrationMask)+",\"calibration_pose\":"+String(s.calibrationPose)+",\"calibration_progress\":"+String(s.calibrationProgress)+",\"calibration_applied\":"+String(s.calibrationApplied?"true":"false")+",\"calibration_generation\":"+String(s.calibrationGeneration)+",\"calibration_save_error\":"+String(s.calibrationSaveError)+",\"calibration_restore_error\":"+String(s.calibrationRestoreError)+",\"calibration_gravity\":"+(isfinite(s.calibrationGravity)?String(s.calibrationGravity,6):String("null"))+",\"calibration_latitude\":"+(isfinite(s.calibrationLatitude)?String(s.calibrationLatitude,6):String("null"));
 out+=",\"calibration_diagnostics\":"+String(fresh&&s.calibrationDiagnostics.available?"true":"false")+",\"calibration_rejection\":"+String(s.calibrationDiagnostics.rejection)+",\"calibration_rejections\":"+String(s.calibrationDiagnostics.resets);
 out+=",\"sd_diagnostics\":"+String(s.sd.available?"true":"false")+",\"sd_state\":\""+String(fresh&&s.sd.available?sprctl::sdStateName(s.sd.state):"UNKNOWN")+"\",\"sd_failure\":\""+String(s.sd.available?sprctl::sdFailureName(s.sd.failure):"UNKNOWN")+"\",\"sd_detail\":"+String(s.sd.detail)+",\"sd_attempts\":"+String(s.sd.attempts)+",\"sd_recoveries\":"+String(s.sd.recoveries)+",\"sd_synced_rows\":"+String(s.sd.syncedRows)+",\"sd_rows\":"+String(s.nav.sd_rows)+",\"imu_gaps\":"+String(s.nav.imu_gaps);
 out+=",\"imu_samples\":"+String(s.nav.imu_samples)+",\"imu_errors\":"+String(s.nav.imu_errors)+",\"sd_errors\":"+String(s.nav.sd_errors)+",\"calibration\":"+String(s.calibration)+",\"last_operation\":"+String(s.lastOperation)+",\"last_result\":"+String(s.lastResult)+",\"command_pending\":"+String(s.commandPending?"true":"false")+",\"command_id\":"+String(s.commandId)+",\"command_op\":"+String(s.commandOp)+",\"command_result\":"+String(s.commandResult)+",\"link_age_ms\":"+(s.packets?int64Text((now-s.receivedUs)/1000):String("null"))+"}";return out;
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
 auto config=acquisitionSnapshot();
 mutex_enter_blocking(&stateMutex);auto local=sharedHealth,remote=remoteHealth;uint32_t age=remoteSeenMs?millis()-remoteSeenMs:0xffffffff;bool sync=syncOk;auto uncertainty=syncUncertainty;auto offset=syncOffset;auto lr=linkRx,le=linkErrors,seq=shotSeq,rs=rangeState;auto tof=lastTof;auto patternId=lastPatternId;mutex_exit(&stateMutex);
 String out="{\"raw_rows\":"+String(rawRows.load())+",\"raw_dropped\":"+String(rawDropped.load())+",\"raw_file\":\""+String(rawName)+"\",\"role\":\""+String(role)+"\",\"ranging_method\":\"tether_edge_one_way\",\"range_calibrated\":false,\"parent_relative_time\":true,\"utc_valid\":"+String(utcSnapshot().valid(time_us_64())?"true":"false")+",\"utc\":"+utcJson()+",\"imu_hz\":"+String(imuRateHz.load())+",\"ranging\":"+String(runRanging?"true":"false")+",\"link_age_ms\":"+String(age)+",\"link_rx\":"+String(lr)+",\"link_errors\":"+String(le)+",\"sync_ok\":"+String(sync?"true":"false")+",\"offset_us\":"+int64Text(offset)+",\"uncertainty_us\":"+String(uncertainty)+",\"shot_seq\":"+String(seq)+",\"range_state\":"+String(rs)+",\"tof_us\":"+int64Text(tof)+",\"range_m\":";
 out+=(rs==2 || rs==3)?String(tof*aq::soundSpeed/1000000.0,4):String("null");
 out+=",\"acoustic_pattern_id\":"+String(patternId)+",\"acoustic_pattern_count\":64,\"ranging_rate_hz\":"+String(config.rate)+",\"acoustic_pattern\":"+String(config.pattern)+",\"log_format\":\"AQBIN001\",\"frequency_hz\":"+String(config.frequency)+",\"observation_seconds\":"+String(config.seconds)+",\"observation_every\":"+String(config.every)+",\"observation_file\":\""+String(observationName)+"\",\"event_dropped\":"+String(eventDropped.load())+",\"capture_request_dropped\":"+String(captureRequestDrops.load())+",\"capture_edge_dropped\":"+String(captureEdgeDrops.load());
 #ifdef BOARD_PARENT
 out+=",\"utc_test\":{\"gps_suppressed\":"+String(testActive(gpsTestUntilMs.load())?"true":"false")+",\"transfer_suppressed\":"+String(testActive(utcTransferTestUntilMs.load())?"true":"false")+"}";
 out+=",\"sd_recovery\":{\"enabled\":"+String(sdRecovery.wanted?"true":"false")+",\"waiting\":"+String(sdRecovery.pending?"true":"false")+",\"exhausted\":"+String(sdRecovery.exhausted()?"true":"false")+",\"attempts\":"+String(sdRecovery.attempts)+",\"recoveries\":"+String(sdRecovery.recoveries)+",\"last_failure\":\""+String(sdLastFailure)+"\"}";
 out+=",\"mag_calibration\":"+magJson()+",\"parent_calibration\":"+parentCalibrationJson();
 out+=",\"rtk\":"+rtkSummaryJson();
 out+=",\"network\":{\"sta_status\":"+String(unsigned(WiFi.status()))+",\"wifi_configured\":"+String(ntrip::ssid.length()?"true":"false")+",\"wifi_attempts\":"+String(ntrip::wifiJoinAttempts)+",\"ap_ip\":\""+WiFi.softAPIP().toString()+"\",\"sta_connected\":"+String(ntrip::online()?"true":"false")+",\"sta_ip\":\""+WiFi.localIP().toString()+"\"}";
 out+=",\"ppk\":{\"recording\":"+String(gnssRecording.load()?"true":"false")+",\"rawx_saved\":"+String(gnssSavedRawx)+",\"sfrbx_saved\":"+String(gnssSavedSfrbx)+",\"bytes\":"+String(gnssSavedBytes)+",\"rtcm_bytes\":"+String(rtcmSavedBytes)+",\"dropped\":"+String(gnssLogDropped.load()-gnssSessionDrops)+gnssStorageDiagnosticsJson()+",\"file\":\""+String(gnssName)+"\",\"rtcm_file\":\""+String(rtcmName)+"\",\"meta_file\":\""+String(gnssMetaName)+"\"}";
 out+=",\"gnss\":"+gnssJson(gnssSnapshot(),time_us_64());auto sp=spresenseSnapshot();out+=",\"spresense\":"+spresenseJson(sp,time_us_64())+",\"phone\":"+phoneJson(time_us_64());
 #else
 out+=",\"sd_recovery\":{\"enabled\":"+String(childSdRecovery.wanted?"true":"false")+",\"waiting\":"+String(childSdRecovery.pending?"true":"false")+",\"exhausted\":"+String(childSdRecovery.exhausted()?"true":"false")+",\"attempts\":"+String(childSdRecovery.attempts)+",\"recoveries\":"+String(childSdRecovery.recoveries)+",\"last_failure\":\""+String(childSdLastFailure)+"\"}";
#endif
 out+=",\"body_calibration\":"+bodyJson();
 out+=",\"range_calibrated\":false,\"sd_reason\":\""+String(sdReason)+"\",\"sd_card_present\":"+String(sdCardPresent()?"true":"false")+",\"sd_spi_mhz\":"+String(SD_SPI_MHZ)+",\"sd_error_code\":"+String(lastSdCode)+",\"sd_error_data\":"+String(lastSdData)+",\"log_file\":\""+String(logName)+"\",\"health_file\":\""+String(slowName)+"\",\"max_write_us\":"+String(maxWriteUs)+",\"local\":"+healthJson(local)+",\"remote\":"+healthJson(remote)+"}";return out;
}
#include "rx_monitor.h"
#include "multipath.h"
#include "experiment_control.h"
#ifdef BOARD_PARENT
#include "web_control.h"
bool parentCalibrationUsb(const char* line){
 if(!strcmp(line,"NET STATUS")){Serial.println(String("{\"network_settings\":")+ntrip::settingsJson()+",\"network_diag\":"+webDiagnosticsJson()+"}");return true;}
 if(!strcmp(line,"NET REJOIN")){ntrip::queueSettingsWifi();Serial.println("{\"network_rejoin_queued\":true}");return true;}
 if(!strcmp(line,"SET STATUS")){Serial.println(String("{\"parent_set\":")+parentCalibrationJson()+"}");return true;}
 if(strcmp(line,"SET SAVE"))return false;
 bool allowed=parentCalibrationCanSave();auto next=parentCalibration;
 if(!parentCalibrationLoaded){next=parentcal::Record{};strcpy(next.name,"親機校正セット");}
 if(allowed)parentCalibrationReferenceCurrent(next);
 bool saved=allowed && saveParentCalibration(next);
 Serial.println(String("{\"parent_set_save\":")+(saved?"true":"false")+",\"allowed\":"+(allowed?"true":"false")+",\"parent_set\":"+parentCalibrationJson()+"}");return true;
}
#endif
void updateSensors(){
 const auto& r=i2c_readings();auto h=health();h.uptimeMs=millis();h.rows=rows;h.durableRows=durableRows;h.sdErrors=sdErrors;h.dropped=dropped.load()+rawDropped.load();
 h.flags&=(aq::IMU|aq::CALIBRATING|aq::CAMERA_READY); if(powered)h.flags|=aq::POWER;if(sdHealthy)h.flags|=aq::SD;
 auto u=utcSnapshot();if(u.valid(time_us_64()))h.flags|=aq::UTC_VALID | (uint16_t(u.source)<<12);if(rtcWrites)h.flags|=aq::RTC_SAVED;
 if(r.ina_ok)h.flags|=aq::INA;if(r.tsys_ok)h.flags|=aq::TEMP;if(r.prs_ok)h.flags|=aq::PRESSURE;if(r.rtc_ok)h.flags|=aq::RTC;if(r.lis_ok)h.flags|=aq::MAG;
 h.volts=r.ina_ok?r.ina_v:NAN;h.currentMa=r.ina_ok?r.ina_ma:NAN;h.temperature=r.tsys_ok?r.tsys_temp:NAN;h.pressure=r.prs_ok?r.prs_mbar:NAN;
 h.magX=r.lis_ok?r.lis_x:NAN;h.magY=r.lis_ok?r.lis_y:NAN;h.magZ=r.lis_ok?r.lis_z:NAN;
 if(!monitorActive
#ifndef BOARD_PARENT
 && !multipathActive
#endif
 )h.buttonsMv=uint32_t(analogRead(PIN_BTN_ADC))*3300/4095;
#ifndef BOARD_PARENT
 // Active LOW, with 100k pull-up and 10nF filter on the board; require 20ms stable wet.
 static uint32_t lowSince=0;
 if(!digitalRead(PIN_LEAK)){if(!lowSince)lowSince=millis();if(millis()-lowSince>=20)h.flags|=aq::LEAK;}else lowSince=0;
 if(!monitorActive && !multipathActive)h.envMv=uint32_t(analogRead(PIN_RX_ENV))*3300/4095;
 if(digitalRead(PIN_RX_COMP))h.flags|=aq::RX_HIGH;
#endif
 setHealth(h);
}
void saveHealth(){
 if(!sdHealthy)return;
 auto ua=utcSnapshot();uint64_t now=time_us_64();
#ifdef BOARD_PARENT
 auto& p=phoneReference;aqbin::TimePosition position{};position.utcMs=ua.at(now)/1000;position.source=ua.valid(now)?ua.source:0;
 position.phoneAgeMs=p.haveTime?int64_t(now/1000-p.anchorMs):-1;position.rtt=p.rttMs;position.positionValid=p.positionFresh(now/1000);position.latitude=p.latE7;position.longitude=p.lonE7;position.accuracyMm=p.accuracyMm;
 if(!writeBinary(logFile,aqbin::TIME_POSITION,now,rows.load(),position))return;
#endif
 aqbin::Health b{};b.sensor=health();mutex_enter_blocking(&stateMutex);b.linkRx=linkRx;b.sync=syncOk;b.offset=syncOffset;b.uncertainty=syncUncertainty;b.shot=shotSeq;b.reference=lastTx;b.acoustic=lastRx;b.tof=lastTof;b.state=rangeState;auto config=acquisitionConfig;auto patternId=lastPatternId;mutex_exit(&stateMutex);
 const auto& rtc=i2c_readings();strncpy(b.rtc,rtc.rtc_time,sizeof(b.rtc)-1);b.rtcTemperature=rtc.rtc_temp;b.utcMono=ua.mono;b.utcEpoch=ua.epoch;b.utcUntil=ua.until;b.utcSource=ua.source;b.frequency=config.frequency;b.observationSeconds=config.seconds;b.observationEvery=config.every;b.rate=config.rate;b.pattern=config.pattern;b.patternId=patternId;b.patternGap1=patternId<aq::patternCount?aq::patternGapUs(patternId,0):0;b.patternGap2=patternId<aq::patternCount?aq::patternGapUs(patternId,1):0;
 if(writeBinary(slowFile,aqbin::HEALTH,now,b.shot,b)){
#ifdef BOARD_PARENT
 rows++;
#endif
 }
}
void setup(){
 Serial.begin(115200);mutex_init(&stateMutex);sessionId=get_rand_32();analogReadResolution(12);
 pinMode(PIN_BTN_ADC,INPUT);pinMode(PIN_LED_STATUS,OUTPUT);pinMode(PIN_SD_CD,INPUT);
#ifdef BOARD_PARENT
 pinMode(PIN_TX_PWM,OUTPUT);digitalWrite(PIN_TX_PWM,LOW);
 pinMode(PIN_GNSS_PPS,INPUT);pinMode(PIN_SPRESENSE_DATA,INPUT);
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
 monitorTick();multipathTick();
#endif
 if(!powered && millis()-powerTry>=1000){
  powerTry=millis();if(uint32_t(analogRead(PIN_BTN_ADC))*3300/4095>=V3D_PRESENT_MIN_MV){
   i2c_init();i2c_sensors_setup();powered=true;mountSd();
   Serial.printf("# POWER_READY %s SD=%s\n",role,sdReason);
  }
 }
 controlTick();
#ifdef BOARD_PARENT
 ntrip::tick();
 ntrip::historyTick(gnssSettingsStorageReady()&&!dumping&&!stopRequested&&!localControlBusy);
 if(sdHealthy)sdRecovery.healthy(millis());
 else if(powered && !stopRequested && !dumping && !localControlBusy && sdRecovery.due(millis())){sdRecovery.attempting(millis());mountSd();}
#else
 if(sdHealthy)childSdRecovery.healthy(millis());
 else if(powered && !stopRequested && !dumping && !localControlBusy && childSdRecovery.due(millis())){childSdRecovery.attempting(millis());mountSd();}
#endif
 if(powered){i2c_sensors_tick(millis());utcTick();bodyTick();
#ifdef BOARD_PARENT
 logMagObservation();
#endif
 if(millis()-lastHealth>=10){lastHealth=millis();updateSensors();}sdTick();if(stopRequested && pauseAck && samples.head.load()==samples.tail.load() && rawImu.head.load()==rawImu.tail.load() && events.head.load()==events.tail.load()
#ifdef BOARD_PARENT
 && gnssFrames.head.load()==gnssFrames.tail.load() && correctionLogs.head.load()==correctionLogs.tail.load()
#endif
 )finishStop();}
#ifdef BOARD_PARENT
 webTick();
#endif
 while(Serial.available()){
  char c=Serial.read();
#ifdef BOARD_PARENT
  if(spresenseUsb(c))continue;
#endif
#ifdef AQ_SD_RECOVERY_TEST
  if(c=='~'){Serial.println("# SD_RECOVERY_TEST ready");continue;}
  if(c=='!'){injectSdRecoveryTest();continue;}
#endif
  if(c=='h'||c=='?'){if(!dumping)Serial.println(statusJson());}
  else if(c=='B' && stopped && !dumping){rp2040.reboot();} // No further watchdog feeding after requesting reset.
  else if(c=='s'){if(!monitorActive)startRecording();}else if(c=='q')stopLog();else if(c=='r')resetBno=true;
#ifndef BOARD_PARENT
 else if(c=='m')monitorStart();
 else if(c=='n')monitorStop();
 else if(c=='v' && !monitorActive && !multipathActive)Serial.printf("# RX_DIAG cmp=%u env_adc=%u threshold_mv=300 leak=%u\n",unsigned(digitalRead(PIN_RX_COMP)),unsigned(analogRead(PIN_RX_ENV)),unsigned(digitalRead(PIN_LEAK)));
#endif
else if(c=='d' || c=='e' || c=='i'
#ifdef BOARD_PARENT
 || c=='u' || c=='o' || c=='k'
#endif
 ){if(stopped && !dumping){const char* name=c=='d'?logName:(c=='i'?rawName:slowName);
#ifdef BOARD_PARENT
 if(c=='u')name=gnssName;if(c=='o')name=rtcmName;if(c=='k')name=gnssMetaName;
#endif
 dumpFile=sd.open(name,O_RDONLY);if(dumpFile){dumping=true;Serial.printf("# DUMP_BEGIN bytes=%llu\n",(unsigned long long)dumpFile.fileSize());}}}else if(c=='b')Serial.printf("# BNO_STAGE %d\n",int(bnoStage));
#ifdef BOARD_PARENT
  else if(c=='F'){gpsTestUntilMs=millis()+90000;}
  else if(c=='G'){gpsTestUntilMs=0;utcTransferTestUntilMs=0;}
  else if(c=='J'){utcTransferTestUntilMs=millis()+15000;}
  else if(c=='T' && !i2c_mag_calibration().active){if(!bodyCameraReady()){Serial.println("# RANGING_REFUSED camera_motion_required");}else{runRanging=true;auto c=acquisitionSnapshot();Serial.printf("# RANGING_ENABLED rate=%u pattern=%u carrier=%luHz\n",c.rate,c.pattern,(unsigned long)c.frequency);}}
  else if(c=='X'){runRanging=false;Serial.println("# RANGING_DISABLED");}
  else if(c=='w')Serial.printf("# WIFI ssid=AquaBeacon-MAIN password=%s ip=%s\n",apPassword,WiFi.softAPIP().toString().c_str());
  else if(c=='W')Serial.println(webDiagnosticsJson());
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
 digitalWrite(PIN_LED_STATUS,(millis()/500)%2);
}
