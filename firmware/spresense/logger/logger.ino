#include <Arduino.h>
#include <GNSS.h>
#include <MP.h>
#include <ImuBatch.h>
#include <SDHCI.h>
#include <AquaBeacon.h>
#include <nuttx/sensors/cxd5602pwbimu.h>
#include <arch/board/cxd56_cxd5602pwbimu.h>
#include <pthread.h>
#include <sched.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <math.h>
#include <atomic>
#include <nuttx/irq.h>

// Sony CXD5602PWBIMU Add-on, official Arduino core 3.4.7.
// SD I/O runs only in loop(); GNSS/UART and IMU acquisition have dedicated tasks.
namespace pins {
constexpr uint8_t ppsFeedback = PIN_D03; // Extension D02 hardware PPS -> D03 input, JP1=3.3V.
constexpr uint8_t gpsReceivedLed = LED0; // Spresense main-board LED0.
}
constexpr uint32_t gpsLedOnMs = 100;

SDClass card;
SpGnss gnss;
std::atomic<bool> gnssHasFix{false};
constexpr uint32_t gnssAcquisitionWaitMs=120000; // Bounded first-fix head start before IMU SPI activity.
using ImuRow=aqimu::Row;
static_assert(sizeof(aqimu::Data)==sizeof(cxd5602pwbimu_data_t), "IMU driver ABI");
aqimu::Batch *batch=nullptr;
bool workerOk=false, workerPending=false;
uint32_t workerStarted=0, workerId=0, workerBatches=0, workerErrors=0;
struct GpsRow { uint64_t received_us; aq::Nav nav; aqimu::Edge pps; };
template<class T, unsigned N> struct Queue {
  T data[N]; std::atomic<unsigned> head{0}, tail{0};
  bool push(const T &v) {
    unsigned h=head.load(std::memory_order_relaxed), next=(h+1)%N;
    if(next==tail.load(std::memory_order_acquire)) return false;
    data[h]=v; head.store(next,std::memory_order_release); return true;
  }
  bool pop(T &v) {
    unsigned t=tail.load(std::memory_order_relaxed);
    if(t==head.load(std::memory_order_acquire)) return false;
    v=data[t]; tail.store((t+1)%N,std::memory_order_release); return true;
  }
};
Queue<ImuRow,1024> imuQueue;
Queue<GpsRow,16> gpsQueue;
std::atomic<uint32_t> imuSamples{0}, imuErrors{0}, imuGaps{0}, queueDrops{0}, gpsDrops{0}, sdErrors{0}, sdRows{0};
std::atomic<uint32_t> gpsReceptionPulses{0};
std::atomic<bool> imuReady{false}, sdReady{false}, recording{true};
pthread_mutex_t recordingMutex=PTHREAD_MUTEX_INITIALIZER;
int imuFile=-1, gpsFile=-1;
char imuPath[64], gpsPath[64];
struct GpsStatus {
  uint32_t sequence=0, txBytes=0, txErrors=0;
  unsigned fix=0, used=0, visible=0, flags=0;
  float maxSignal=0;
  uint32_t firstFixMs=0;
};
GpsStatus liveGps;
pthread_mutex_t gpsStatusMutex=PTHREAD_MUTEX_INITIALIZER;

// Use the same 64-bit Arduino power-on clock for both IRQ and task timestamps.
// Core 3.4.7 micros() reads the hardware RTC counter; this is NOT UTC/RTC calendar.
uint64_t monoUs() { return micros(); }
volatile uint64_t ppsUs=0;
volatile uint32_t ppsCount=0;
void ppsInterrupt() { ppsUs=micros(); ++ppsCount; }
aqimu::Edge snapshotPps() {
  irqstate_t state=enter_critical_section();
  aqimu::Edge e{ppsUs,ppsCount};
  leave_critical_section(state); return e;
}
aqimu::UtcClock imuUtc;
pthread_mutex_t utcMutex=PTHREAD_MUTEX_INITIALIZER;
void *imuTask(void *) {
  uint64_t waitStarted=monoUs();
  while(!gnssHasFix && monoUs()-waitStarted<uint64_t(gnssAcquisitionWaitMs)*1000) usleep(100000);
  Serial.printf("# IMU_START gnss_fix=%u wait_ms=%lu\n",int(gnssHasFix.load()),(unsigned long)((monoUs()-waitStarted)/1000));
  if(board_cxd5602pwbimu_initialize(5)<0) { ++imuErrors; return nullptr; }
  int fd=open("/dev/imu0",O_RDONLY|O_NONBLOCK);
  cxd5602pwbimu_range_t range{4,500};
  if(fd<0) { ++imuErrors; return nullptr; }
  if(ioctl(fd,SNIOC_SSAMPRATE,aqimu::rate) || ioctl(fd,SNIOC_SDRANGE,(unsigned long)(uintptr_t)&range)
     || ioctl(fd,SNIOC_SFIFOTHRESH,1) || ioctl(fd,SNIOC_ENABLE,1)) {
    ++imuErrors; close(fd); return nullptr;
  }
  imuReady=true;
  uint32_t previous=0; bool havePrevious=false;
  while(true) {
    pollfd p{fd,POLLIN,0}; int ready=poll(&p,1,1000);
    if(ready<0 && errno==EINTR) continue;
    if(ready<=0 || !(p.revents&POLLIN)) { ++imuErrors; imuReady=false; continue; }
    ImuRow row{};
    ssize_t n=read(fd,&row.data,sizeof(row.data));
    if(n<0 && (errno==EAGAIN || errno==EINTR)) continue;
    if(n!=sizeof(row.data)) { ++imuErrors; continue; }
    row.received_us=monoUs(); row.seq=++imuSamples; imuReady=true;
    pthread_mutex_lock(&utcMutex);
    row.utc=imuUtc.at(row.received_us,snapshotPps());
    pthread_mutex_unlock(&utcMutex);
    // Sony's reference uses a 19.2 MHz timestamp counter. Do not learn the
    // threshold from the smallest observed interval: one short interval would
    // incorrectly mark every later sample as missing. Count discontinuity
    // events (not estimated lost samples); preserve raw data for analysis.
    if(havePrevious) {
      uint32_t step=row.data.timestamp-previous;
      constexpr uint32_t nominalStep=19200000UL/aqimu::rate;
      if(!step || step>nominalStep+nominalStep/2) ++imuGaps;
    }
    previous=row.data.timestamp; havePrevious=true;
    pthread_mutex_lock(&recordingMutex);
    if(recording && !imuQueue.push(row)) ++queueDrops;
    pthread_mutex_unlock(&recordingMutex);
  }
  return nullptr;
}
void *gpsTask(void *) {
  Serial2.begin(115200); // Extension D01 TX, D00 RX, JP10 1-2 open.
  gnss.setDebugMode(PrintNone);
  // Match the locally proven standalone Sony sample; retain cold start so
  // comparisons do not depend on an unverified ephemeris cache.
  if(gnss.begin()!=0 || gnss.select(GPS)!=0 || gnss.select(GLONASS)!=0 ||
     gnss.select(QZ_L1CA)!=0 || gnss.setInterval(1L)!=0 || gnss.start(COLD_START)!=0) {
    Serial.println("# GNSS_START_FAILED"); return nullptr;
  }
  Serial.println("# GNSS_CONFIG GPS+GLONASS+QZSS_L1CA cold_start interval_s=1");
  uint64_t gnssStarted=monoUs();
  gnss.start1PPS(); // Hardware GNSS output on D02, never software generated.
  aq::Nav out;
  while(true) {
    if(!gnss.waitUpdate(1)) continue;
    SpNavData nav{}; gnss.getNavData(&nav);
    uint64_t received=monoUs();
    aqimu::Edge edge=snapshotPps();
    out.sequence++; out.flags=0; out.fix=nav.posFixMode; out.satellites=nav.numSatellitesCalcPos;
    out.utc_s=aq::unix_seconds(nav.time.year,nav.time.month,nav.time.day,nav.time.hour,nav.time.minute,nav.time.sec);
    out.usec=nav.time.usec;
    bool fix=nav.type==SpPvtTypeGnss && nav.posDataExist && (nav.posFixMode==Fix2D || nav.posFixMode==Fix3D);
    if(fix) gnssHasFix=true;
    if(fix && out.utc_s && out.usec<=1000) out.flags|=aq::time_valid;
    if(fix && isfinite(nav.latitude) && isfinite(nav.longitude) && isfinite(nav.altitude)
       && fabs(nav.latitude)<=90 && fabs(nav.longitude)<=180 && fabs(nav.altitude)<2000000) {
      out.flags|=aq::position_valid;
      out.lat_e7=lround(nav.latitude*1e7); out.lon_e7=lround(nav.longitude*1e7); out.altitude_mm=lround(nav.altitude*1000);
    } else { out.lat_e7=out.lon_e7=out.altitude_mm=0; }
    pthread_mutex_lock(&utcMutex);
    imuUtc.update(out.flags & aq::time_valid,out.utc_s,out.usec,out.sequence,received,edge);
    pthread_mutex_unlock(&utcMutex);
    if(sdReady) out.flags|=aq::sd_ok;
    if(imuReady) out.flags|=aq::imu_ok;
    out.imu_samples=imuSamples; out.imu_errors=imuErrors; out.imu_gaps=imuGaps.load()+queueDrops.load();
    out.sd_errors=sdErrors.load()+gpsDrops.load(); out.sd_rows=sdRows; out.uptime_ms=uint32_t(received/1000);
    uint8_t frame[aq::frame_size]; aq::encode(out,frame);
    size_t sent=Serial2.write(frame,sizeof(frame)); // Send before any SD operation.
    float maxSignal=0;
    for(unsigned i=0;i<nav.numSatellites && i<24;++i) {
      float signal=nav.getSatelliteSignalLevel(i);
      if(isfinite(signal) && signal>maxSignal) maxSignal=signal;
    }
    // A visible satellite means that the GNSS receiver is receiving a signal;
    // a position fix is intentionally not required for this bring-up indicator.
    if(nav.numSatellites>0) ++gpsReceptionPulses;
    pthread_mutex_lock(&gpsStatusMutex);
    if(fix && !liveGps.firstFixMs) liveGps.firstFixMs=uint32_t((received-gnssStarted)/1000)+1;
    liveGps.sequence=out.sequence; liveGps.fix=out.fix; liveGps.used=out.satellites;
    liveGps.visible=nav.numSatellites; liveGps.flags=out.flags; liveGps.maxSignal=maxSignal;
    liveGps.txBytes+=sent; if(sent!=sizeof(frame)) ++liveGps.txErrors;
    pthread_mutex_unlock(&gpsStatusMutex);
    pthread_mutex_lock(&recordingMutex);
    if(recording && !gpsQueue.push(GpsRow{received,out,edge})) ++gpsDrops;
    pthread_mutex_unlock(&recordingMutex);
  }
  return nullptr;
}
bool saveLine(int fd,const char *line,bool checksum=true) {
  size_t len=strlen(line);
  char protectedLine[420];
  if(checksum) {
    if(len && line[len-1]=='\n') --len;
    if(len+12>=sizeof(protectedLine)) { ++sdErrors; sdReady=false; return false; }
    memcpy(protectedLine,line,len);
    snprintf(protectedLine+len,sizeof(protectedLine)-len,",%08lx\n",(unsigned long)aq::crc32((const uint8_t*)line,len));
    line=protectedLine; len=strlen(line);
  }
  if(fd<0 || write(fd,line,len)!=(ssize_t)len) { ++sdErrors; sdReady=false; return false; }
  ++sdRows; return true;
}
void failWorker() {
  workerOk=false; workerPending=false; ++workerErrors; ++sdErrors;
  sdReady=false;
  pthread_mutex_lock(&recordingMutex); recording=false; pthread_mutex_unlock(&recordingMutex);
  Serial.println("# FORMATTER_FAILED recording stopped");
}
void serviceFormatter() {
  if(!workerOk) { ImuRow discard; while(imuQueue.pop(discard)) ++queueDrops; return; }
  if(workerPending) {
    int8_t message=0; uint32_t response=0;
    int ret=MP.Recv(&message,&response,1);
    if(ret>=0) {
      __sync_synchronize();
      if(message!=11 || response!=batch->id || batch->version!=aqimu::magic || batch->error ||
         !batch->length || batch->length>sizeof(batch->csv) || batch->count>aqimu::batchSize) { failWorker(); return; }
      workerPending=false; ++workerBatches;
      if(sdReady) {
        // One SD operation per batch; no float formatting on the acquisition core.
        if(write(imuFile,batch->csv,batch->length)!=(ssize_t)batch->length) { failWorker(); return; }
        sdRows+=batch->count;
      }
    } else if(millis()-workerStarted>2000) { failWorker(); return; }
  }
  if(!workerPending) {
    batch->count=0;
    while(batch->count<aqimu::batchSize && imuQueue.pop(batch->rows[batch->count])) ++batch->count;
    if(batch->count) {
      batch->version=aqimu::magic; batch->id=++workerId; batch->error=1; batch->length=0;
      __sync_synchronize();
      if(MP.Send(10,(void*)batch,1)<0) { failWorker(); return; }
      workerPending=true; workerStarted=millis();
    }
  }
}
bool openLogs() {
  if(!card.begin()) return false;
  char path[64];
  int id=0;
  for(;id<100000;id++) {
    snprintf(imuPath,sizeof(imuPath),"/mnt/sd0/I%05d.CSV",id);
    snprintf(gpsPath,sizeof(gpsPath),"/mnt/sd0/G%05d.CSV",id);
    snprintf(path,sizeof(path),"/mnt/sd0/V%05d.BIN",id);
    if(access(imuPath,F_OK)!=0 && access(gpsPath,F_OK)!=0 && access(path,F_OK)!=0) break;
  }
  if(id==100000) return false;
  int test=open(path,O_CREAT|O_EXCL|O_RDWR,0666);
  if(test<0) return false;
  uint8_t pattern[512], back[512];
  for(size_t i=0;i<sizeof(pattern);i++) pattern[i]=uint8_t(i*37+id);
  bool ok=write(test,pattern,sizeof(pattern))==sizeof(pattern) && fsync(test)==0;
  ok=(close(test)==0) && ok;
  test=open(path,O_RDONLY);
  ok=test>=0 && read(test,back,sizeof(back))==sizeof(back) && !memcmp(pattern,back,sizeof(back)) && ok;
  if(test>=0) close(test);
  unlink(path); // Only the exclusively created verification file.
  if(!ok) return false;
  imuFile=open(imuPath,O_CREAT|O_EXCL|O_WRONLY,0666);
  gpsFile=open(gpsPath,O_CREAT|O_EXCL|O_WRONLY,0666);
  if(imuFile<0 || gpsFile<0) {
    if(imuFile>=0) close(imuFile);
    if(gpsFile>=0) close(gpsFile);
    imuFile=gpsFile=-1; return false;
  }
  return saveLine(imuFile,"seq,received_mono_us,sensor_timestamp_raw,temp,gx,gy,gz,ax,ay,az,utc_received_us,utc_valid,utc_sync_valid,utc_source,utc_age_us,utc_anchor_seq,crc32\n",false) &&
    saveLine(gpsFile,"seq,received_mono_us,utc_s,nav_usec,flags,fix,satellites,lat_e7,lon_e7,altitude_mm,imu_samples,imu_errors,imu_gaps,sd_errors,sd_rows,pps_mono_us,pps_count,crc32\n",false);
}
bool startTask(void *(*fn)(void *), int priority) {
  pthread_attr_t a; pthread_attr_init(&a); pthread_attr_setstacksize(&a,16384);
  sched_param scheduling{}; scheduling.sched_priority=priority;
  if(pthread_attr_setinheritsched(&a,PTHREAD_EXPLICIT_SCHED)!=0 ||
     pthread_attr_setschedpolicy(&a,SCHED_FIFO)!=0 ||
     pthread_attr_setschedparam(&a,&scheduling)!=0) {
    pthread_attr_destroy(&a); return false;
  }
  pthread_t t; int r=pthread_create(&t,&a,fn,nullptr); pthread_attr_destroy(&a);
  if(!r) pthread_detach(t); return r==0;
}
void serviceGpsLed() {
  static uint32_t handledPulses=0, turnedOnAt=0;
  static bool on=false;
  uint32_t now=millis();
  uint32_t pulses=gpsReceptionPulses.load();
  if(pulses!=handledPulses) {
    handledPulses=pulses;
    turnedOnAt=now;
    on=true;
    ledOn(pins::gpsReceivedLed);
  }
  // Subtraction remains correct across the millis() wraparound.
  if(on && uint32_t(now-turnedOnAt)>=gpsLedOnMs) {
    on=false;
    ledOff(pins::gpsReceivedLed);
  }
}
void exportStoppedLog(const char *path) {
  if(recording || imuFile>=0 || gpsFile>=0) { Serial.println("# EXPORT_REFUSED stop with q first"); return; }
  int fd=open(path,O_RDONLY);
  if(fd<0) { Serial.println("# EXPORT_OPEN_FAILED"); return; }
  Serial.println("# EXPORT_BEGIN");
  uint8_t buffer[512]; ssize_t count; bool ok=true;
  while((count=read(fd,buffer,sizeof(buffer)))>0) {
    if(Serial.write(buffer,size_t(count))!=size_t(count)) { ok=false; break; }
  }
  if(count<0) ok=false;
  if(close(fd)!=0) ok=false;
  Serial.println(ok ? "# EXPORT_END" : "# EXPORT_FAILED");
}
void setup() {
  Serial.begin(115200);
  ledOff(pins::gpsReceivedLed);
  pinMode(pins::ppsFeedback,INPUT_PULLDOWN);
  attachInterrupt(digitalPinToInterrupt(pins::ppsFeedback),ppsInterrupt,RISING);
  Serial.println("# IMU_UTC schema=2 D02_to_D03 required_for_pps timebase=arduino_power_on_us");
  int mpResult=MP.begin(1);
  if(mpResult>=0) batch=static_cast<aqimu::Batch*>(MP.AllocSharedMemory(sizeof(aqimu::Batch)));
  workerOk=mpResult>=0 && batch;
  MP.RecvTimeout(MP_RECV_POLLING);
  Serial.printf("# FORMATTER subcore=1 ready=%u result=%d rate_hz=%lu shared_bytes=%u\n",workerOk,mpResult,(unsigned long)aqimu::rate,sizeof(aqimu::Batch));
  if(!workerOk) { ++workerErrors; recording=false; }
  sdReady=openLogs(); if(!sdReady) ++sdErrors;
  Serial.println(sdReady ? "# SD_READBACK_OK" : "# SD_FAILED");
  Serial.println(imuPath); Serial.println(gpsPath);
  if(!startTask(imuTask,150)) ++imuErrors;
  if(!startTask(gpsTask,120)) Serial.println("# GNSS_THREAD_FAILED");
}
void loop() {
  static uint32_t lastFlush=0, lastStatus=0;
  serviceGpsLed();
  char line[360];
  GpsRow gps;
  while(gpsQueue.pop(gps)) {
    aq::Nav &n=gps.nav;
    snprintf(line,sizeof(line),"%lu,%llu,%llu,%lu,%u,%u,%u,%ld,%ld,%ld,%lu,%lu,%lu,%lu,%lu,%llu,%lu\n",
      (unsigned long)n.sequence,(unsigned long long)gps.received_us,(unsigned long long)n.utc_s,(unsigned long)n.usec,n.flags,n.fix,n.satellites,
      (long)n.lat_e7,(long)n.lon_e7,(long)n.altitude_mm,(unsigned long)n.imu_samples,(unsigned long)n.imu_errors,(unsigned long)n.imu_gaps,(unsigned long)n.sd_errors,(unsigned long)n.sd_rows,(unsigned long long)gps.pps.us,(unsigned long)gps.pps.count);
    if(sdReady) saveLine(gpsFile,line);
  }
  serviceFormatter();
  if(sdReady && millis()-lastFlush>=1000) {
    lastFlush=millis();
    if(fsync(imuFile)!=0 || fsync(gpsFile)!=0) { ++sdErrors; sdReady=false; }
  }
  if(Serial.available()) {
    int c=Serial.read();
    if(c=='q') {
      pthread_mutex_lock(&recordingMutex);
      recording=false;
      pthread_mutex_unlock(&recordingMutex);
      // Finish draining queues before closing on a subsequent loop.
    }
    if(c=='i') exportStoppedLog(imuPath);
    if(c=='g') exportStoppedLog(gpsPath);
  }
  if(!recording && !workerPending && imuQueue.head==imuQueue.tail && gpsQueue.head==gpsQueue.tail && imuFile>=0) {
    bool ok=fsync(imuFile)==0 && fsync(gpsFile)==0 && workerErrors==0;
    ok=(close(imuFile)==0) && ok; ok=(close(gpsFile)==0) && ok;
    imuFile=gpsFile=-1; sdReady=false; if(!ok) ++sdErrors;
    Serial.println(ok ? "# STOPPED_REMOVE_SD" : "# STOP_FAILED");
  }
  if(millis()-lastStatus>=1000) {
    lastStatus=millis();
    snprintf(line,sizeof(line),"# imu=%lu imu_errors=%lu gaps=%lu queue_drops=%lu gps_drops=%lu sd=%d sd_rows=%lu sd_errors=%lu",
      (unsigned long)imuSamples.load(),(unsigned long)imuErrors.load(),(unsigned long)imuGaps.load(),(unsigned long)queueDrops.load(),
      (unsigned long)gpsDrops.load(),int(sdReady.load()),(unsigned long)sdRows.load(),(unsigned long)sdErrors.load());
    Serial.println(line);
    Serial.printf("# formatter_batches=%lu formatter_errors=%lu pending=%u rate_hz=%lu\n",(unsigned long)workerBatches,(unsigned long)workerErrors,workerPending,(unsigned long)aqimu::rate);
    pthread_mutex_lock(&gpsStatusMutex); GpsStatus g=liveGps; pthread_mutex_unlock(&gpsStatusMutex);
    aqimu::Edge edge=snapshotPps();
    pthread_mutex_lock(&utcMutex); aqimu::Stamp stamp=imuUtc.at(monoUs(),edge); pthread_mutex_unlock(&utcMutex);
    Serial.printf("# pps_count=%lu utc_source=%lu utc_sync_valid=%lu utc_age_us=%llu\n",(unsigned long)edge.count,(unsigned long)stamp.source,(unsigned long)stamp.sync_valid,(unsigned long long)stamp.age_us);
    Serial.printf("# gnss_first_fix_ms=%lu\n",(unsigned long)g.firstFixMs);
    Serial.printf("# gps_seq=%lu fix=%u satellites=%u visible=%u max_signal=%.1f flags=%u tx_bytes=%lu tx_errors=%lu\n",
      (unsigned long)g.sequence,g.fix,g.used,g.visible,g.maxSignal,g.flags,(unsigned long)g.txBytes,(unsigned long)g.txErrors);
  }
  usleep(1000); // Yield MainCore task; sensor poll and worker run independently.
}
