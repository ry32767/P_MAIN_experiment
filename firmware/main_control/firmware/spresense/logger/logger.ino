#include <Arduino.h>
#include <LowPower.h>
#include <SpresenseControl.h>
#include <arch/board/board.h>
#include <arch/chip/pin.h>
#include <MP.h>
#include <ImuBatch.h>
#include <sensor_time.h>
#include <spresense_pins.h>
#include <SDHCI.h>
#include <AquaBeacon.h>
#include <SpresenseSdRecovery.h>
#include <sd_sector_write.h>
#include <Watchdog.h>
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
#include <sys/statfs.h>
#include <dirent.h>

// Sony CXD5602PWBIMU Add-on, official Arduino core 3.4.7.
// SD I/O runs only in loop(); parent link and IMU acquisition have dedicated tasks.
// Returned SD errors use bounded retries.  If an SD syscall never returns, the
// hardware watchdog reboots the board and openLogs() selects new file names.
namespace pins {
constexpr uint8_t data = AQ_SPR_DATA_PIN; // GP7 <-> extension D03. JP1=3.3V.
constexpr uint8_t pps = AQ_SPR_PPS_PIN; // J_SPR1.1 GNSS PPS -> D02 INPUT only.
constexpr uint32_t dataGpio = PIN_PWM3; // Sony pin_convert(PIN_D03).
constexpr uint8_t gpsReceivedLed = LED0; // Spresense main-board LED0.
}
constexpr uint32_t gpsLedOnMs = 100;
constexpr uint32_t watchdogTimeoutMs = 15000;

SDClass card;
sprwire::Uart linkWire;
std::atomic<uint32_t> linkPackets{0},linkTimeouts{0},linkCrcErrors{0},timerTicks{0};
std::atomic<uint32_t> linkEdges{0},linkBytes{0};
std::atomic<uint8_t> pendingOperation{0},lastOperation{0},lastOperationResult{0};
std::atomic<bool> safelyStopped{false},rebootRequested{false};
sprcal::Session calibrationSession;
sprcal::Coefficients appliedCalibration;
pthread_mutex_t calibrationMutex=PTHREAD_MUTEX_INITIALIZER;
std::atomic<uint8_t> calibrationSaveError{0},calibrationRestoreError{0};
std::atomic<uint8_t> recentMotionAxes{0};
sprctl::Reference parentReference;
pthread_mutex_t referenceMutex=PTHREAD_MUTEX_INITIALIZER;
unsigned int linkTimer() {
  static bool low=false;
  static bool previousHigh=true;bool high=board_gpio_read(pins::dataGpio);
  if(high!=previousHigh){++linkEdges;previousHigh=high;}
  bool next=linkWire.tick(high,micros());
  if(next!=low){board_gpio_write(pins::dataGpio,next?0:-1);low=next;}
  ++timerTicks;return sprwire::tick_us;
}
std::atomic<bool> operationBusy{false};
using ImuRow=aqimu::Row;
static_assert(sizeof(aqimu::Data)==sizeof(cxd5602pwbimu_data_t), "IMU driver ABI");
aqimu::Batch *batch=nullptr,*batchBuffers=nullptr;
bool workerOk=false, workerPending=false;
uint32_t workerStarted=0, workerId=0, workerBatches=0, workerErrors=0;
uint64_t maxSdWriteUs=0;uint32_t maxFormatterMs=0;
struct GpsRow { uint64_t received_us; aq::Nav nav; uint8_t source; uint64_t ppsMono=0,ppsUtc=0;uint32_t ppsCount=0,ppsPeriod=0,set=0,parent=0;uint8_t paired=0;
 GpsRow(uint64_t t=0,aq::Nav n={},uint8_t s=0):received_us(t),nav(n),source(s){} };
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
Queue<sensortime::Edge,16> ppsQueue;
std::atomic<uint32_t> ppsCount{0},ppsDrops{0};
std::atomic<bool> imuTimeReady{false};
sensortime::PpsClock ppsClock;
uint32_t selectedSet=0,parentSession=0,seenPpsDrops=0;
ImuRow latestTimedRow{}; // Protected by referenceMutex; USB diagnostics only.
void ppsInterrupt(){sensortime::Edge e{micros(),++ppsCount};if(!ppsQueue.push(e))++ppsDrops;}
Queue<ImuRow,1024> imuQueue;
Queue<GpsRow,16> gpsQueue;
std::atomic<uint32_t> imuSamples{0}, imuErrors{0}, imuGaps{0}, queueDrops{0}, gpsDrops{0}, sdErrors{0}, sdRows{0};
std::atomic<uint32_t> gpsReceptionPulses{0};
std::atomic<bool> imuReady{false}, sdReady{false}, recording{true};
pthread_mutex_t recordingMutex=PTHREAD_MUTEX_INITIALIZER;
int imuFile=-1, gpsFile=-1;
char imuPath[64], gpsPath[64];
aq::SpresenseSdRecovery sdRecovery;
const char *sdLastFailure="NONE";
int sdLastDetail=0;
std::atomic<uint8_t> sdFailure{sprctl::SD_NONE},sdState{sprctl::SD_STOPPED},sdAttempts{0};
std::atomic<int32_t> sdDetail{0};
std::atomic<uint32_t> sdRecoveries{0},sdSyncedRows{0};
bool openFailed(uint8_t stage,int error){
 sdFailure=stage;sdDetail=error;errno=error;
 Serial.printf("# SD_OPEN_FAILED stage=%s errno=%d\n",sprctl::sdFailureName(stage),error);return false;
}
void publishSdState(){
 sdState=sdReady && recording?sprctl::SD_RECORDING:
  workerErrors || sdRecovery.exhausted()?sprctl::SD_ERROR:
  sdRecovery.pending?sprctl::SD_RECOVERING:sprctl::SD_STOPPED;
 sdAttempts=sdRecovery.attempts;sdRecoveries=sdRecovery.recoveries;
}
// Power-on monotonic time only; no GPS/UTC conversion in IMU acquisition.
uint64_t monoUs() {
  // Sony Arduino 3.4.7 micros() is already uint64_t and reads the 32768 Hz
  // power-on RTC counter. CLOCK_MONOTONIC in this SDK has a 10 ms scheduler
  // tick and cannot gate ~1 ms IMU samples or short link turnaround intervals.
  return micros();
}
void *imuTask(void *) {
  // Acquisition starts independently of parent reference UTC.
  Serial.println("# IMU_START parent_utc_not_required wait_ms=0");
  if(board_cxd5602pwbimu_initialize(5)<0) { ++imuErrors; return nullptr; }
  int fd=open("/dev/imu0",O_RDONLY|O_NONBLOCK);
  cxd5602pwbimu_range_t range{4,500};
  if(fd<0) { ++imuErrors; return nullptr; }
  if(ioctl(fd,SNIOC_SSAMPRATE,aqimu::rate) || ioctl(fd,SNIOC_SDRANGE,(unsigned long)(uintptr_t)&range)
     || ioctl(fd,SNIOC_SFIFOTHRESH,1) || ioctl(fd,SNIOC_ENABLE,1)) {
    ++imuErrors; close(fd); return nullptr;
  }
  imuReady=true;
  sensortime::ImuClock sensorClock;
  uint64_t started=monoUs();
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
    if(row.received_us-started<50000)continue; // Sony: first 50ms are invalid after enable.
    row.sample_mono_us=sensorClock.feed(row.data.timestamp,row.received_us);
    row.sensor_ticks=sensorClock.extended;row.clock_generation=sensorClock.generation;imuTimeReady=sensorClock.ready;
    pthread_mutex_lock(&referenceMutex);
    sensortime::Edge captured;while(ppsQueue.pop(captured))ppsClock.edge(captured);
    if(ppsDrops!=seenPpsDrops){seenPpsDrops=ppsDrops;ppsClock.invalidate();}
    row.sample_utc_us=row.sample_mono_us?ppsClock.utc(row.sample_mono_us,row.received_us):0;
    row.time_quality=row.sample_utc_us?2:row.sample_mono_us?1:0;row.pps_count=ppsClock.latest.count;
    row.set_generation=selectedSet;row.parent_session=parentSession;
    latestTimedRow=row;
    pthread_mutex_unlock(&referenceMutex);
    // Sony's reference uses a 19.2 MHz timestamp counter. Do not learn the
    // threshold from the smallest observed interval: one short interval would
    // incorrectly mark every later sample as missing. Count discontinuity
    // events (not estimated lost samples); preserve raw data for analysis.
    bool sampleGap=havePrevious && !sensortime::sampleIntervalValid(row.data.timestamp-previous,aqimu::rate);
    if(sampleGap)++imuGaps;
    previous=row.data.timestamp; havePrevious=true;
    float a[3]={row.data.ax,row.data.ay,row.data.az},g[3]={row.data.gx,row.data.gy,row.data.gz};
    pthread_mutex_lock(&calibrationMutex);
    calibrationSession.feed(a,g,row.received_us,sampleGap);
    sprcal::apply(appliedCalibration,a,g,row.corrected+3,row.corrected);
    row.calibration=appliedCalibration.generation;
    pthread_mutex_unlock(&calibrationMutex);
    uint8_t moved=0;for(unsigned i=0;i<3;i++)if(isfinite(row.corrected[i])&&fabsf(row.corrected[i])>.08f)moved|=1u<<i;
    if(moved)recentMotionAxes.fetch_or(moved);
    pthread_mutex_lock(&recordingMutex);
    if(recording && !imuQueue.push(row)) ++queueDrops;
    pthread_mutex_unlock(&recordingMutex);
  }
  return nullptr;
}
void *linkTask(void *) {
  sprctl::Parser parser;sprctl::Replay replay;
  sprctl::Message request,response;uint64_t replyDue=0,lastRequest=0;
  bool replyPending=false,resetReply=false;uint64_t txDeadline=0;uint8_t frame[sprctl::frame_size];
  aq::Nav nav;uint32_t sequence=0;
  while(true) {
    uint64_t now=monoUs();
    for(unsigned budget=0;budget<256;budget++) {
      uint64_t receivedTickUs=0;int c=linkWire.read(&receivedTickUs);if(c<0)break;++linkBytes;
      if(!parser.push(uint8_t(c),receivedTickUs,request))continue;
      if(request.kind!=sprctl::REQUEST || request.result!=sprctl::OK || replyPending || linkWire.busy())continue;
      ++linkPackets;++gpsReceptionPulses;lastRequest=now;
      int cached=replay.lookup(request,response);
      if(cached<0)continue;
      if(!cached) {
        response=request;response.kind=sprctl::RESPONSE;response.result=sprctl::OK;
        pthread_mutex_lock(&referenceMutex);
        bool validTime=parentReference.apply(request,now);
        sensortime::Edge captured;while(ppsQueue.pop(captured))ppsClock.edge(captured);
        if(ppsDrops!=seenPpsDrops){seenPpsDrops=ppsDrops;ppsClock.invalidate();}
        if(request.data[25]==0xa3 && request.data[8]==1)ppsClock.pair(sprctl::get64(request.data+26),sprctl::get64(request.data+34),receivedTickUs,sprwire::receive_us+sprwire::tick_us);
        else ppsClock.invalidate();
        selectedSet=spnav::get32(request.data+46);parentSession=spnav::get32(request.data+50);
        uint64_t epoch=parentReference.at(now);uint8_t source=parentReference.valid(now)?parentReference.source:0;
        pthread_mutex_unlock(&referenceMutex);
        if(!validTime)response.result=sprctl::INVALID;
        else if(resetReply || rebootRequested)response.result=sprctl::BUSY;
        else if(request.op==sprctl::STOP || request.op==sprctl::START) {
          bool empty=false;
          pthread_mutex_lock(&calibrationMutex);bool calibrating=calibrationSession.active();pthread_mutex_unlock(&calibrationMutex);
          if(request.op==sprctl::START && calibrating)response.result=sprctl::BUSY;
          else if(operationBusy.compare_exchange_strong(empty,true)){pendingOperation=request.op;response.result=sprctl::ACCEPTED;}
          else response.result=sprctl::BUSY;
        } else if(request.op==sprctl::RESET) {
          if(resetReply || rebootRequested || !safelyStopped || operationBusy.load())response.result=sprctl::BUSY;
          else {response.result=sprctl::ACCEPTED;resetReply=true;}
        } else if(request.op>=sprctl::CAL_START && request.op!=sprctl::CAL_STATUS) {
          pthread_mutex_lock(&calibrationMutex);
          bool safe=!recording && safelyStopped && !operationBusy;
          if(request.op==sprctl::CAL_END && !operationBusy){calibrationSession.end();}
          else if(!safe || (request.op!=sprctl::CAL_CLEAR && !imuReady))response.result=sprctl::BUSY;
          else if(request.op==sprctl::CAL_START) {
            response.result=calibrationSession.begin(sprcal::getFloat(request.data+16),sprcal::getFloat(request.data+20),now)?sprctl::OK:sprctl::INVALID;
          } else if(request.op==sprctl::CAL_POSE) {
            response.result=calibrationSession.select(request.data[24],now)?sprctl::OK:sprctl::INVALID;
          } else if(request.op==sprctl::CAL_END) {calibrationSession.end();}
          else if(request.op==sprctl::CAL_SAVE || request.op==sprctl::CAL_CLEAR) {
            if(request.op==sprctl::CAL_SAVE && calibrationSession.state!=sprcal::READY)response.result=sprctl::INVALID;
            else {operationBusy=true;pendingOperation=request.op;response.result=sprctl::ACCEPTED;}
          }
          lastOperation=request.op;lastOperationResult=response.result;
          pthread_mutex_unlock(&calibrationMutex);
        }
        memset(response.data,0,sizeof(response.data));
        nav={};nav.sequence=++sequence;nav.uptime_ms=now/1000;
        nav.utc_s=epoch/1000000;nav.usec=epoch%1000000;
        nav.flags=(epoch?aq::time_valid:0)|(sdReady?aq::sd_ok:0)|(imuReady?aq::imu_ok:0);
        nav.imu_samples=imuSamples;nav.imu_errors=imuErrors;nav.imu_gaps=imuGaps.load()+queueDrops.load();
        nav.sd_errors=sdErrors.load()+gpsDrops.load();nav.sd_rows=sdRows;
        aq::encode(nav,response.data);
        response.data[64]=recording;
        pthread_mutex_lock(&calibrationMutex);calibrationSession.expire(now);
        response.data[65]=calibrationSession.state;
        response.data[70]=calibrationSession.mask;response.data[71]=calibrationSession.pose;
        response.data[72]=calibrationSession.progress;response.data[73]=appliedCalibration.enabled;
        response.data[74]=calibrationSaveError;response.data[75]=calibrationRestoreError;
        spnav::put32(response.data+76,appliedCalibration.generation);
        sprcal::putFloat(response.data+80,calibrationSession.active()?calibrationSession.candidate.gravity:appliedCalibration.gravity);
        sprcal::putFloat(response.data+84,calibrationSession.active()?calibrationSession.candidate.latitude:appliedCalibration.latitude);
        sprctl::SdDiagnostics diagnostic;diagnostic.state=sdState;diagnostic.failure=sdFailure;
        diagnostic.attempts=sdAttempts;diagnostic.detail=sdDetail;
        diagnostic.recoveries=sdRecoveries;diagnostic.syncedRows=sdSyncedRows;
        sprctl::encodeSd(diagnostic,response.data);
        uint8_t calibrationRejection=calibrationSession.lastRejection;uint32_t calibrationResets=calibrationSession.resets;
        pthread_mutex_unlock(&calibrationMutex);
        pthread_mutex_lock(&referenceMutex);
        auto timingNav=nav;timingNav.satellites=0xa3;timingNav.lat_e7=ppsClock.latest.count;timingNav.lon_e7=ppsClock.period;
        sprctl::encodeCalibrationDiagnostics(timingNav,calibrationRejection,calibrationResets);sprctl::encodeMotion(timingNav,recentMotionAxes.exchange(0));aq::encode(timingNav,response.data);
        response.data[66]=(epoch?1:0)|(ppsClock.fresh(now)?2:0)|(ppsClock.locked(now)?4:0)|(imuTimeReady?8:0);
        pthread_mutex_unlock(&referenceMutex);response.data[67]=source;
        response.data[68]=lastOperation;response.data[69]=lastOperationResult;
        replay.save(request,response);
        pthread_mutex_lock(&recordingMutex);
        GpsRow timing{now,nav,source};pthread_mutex_lock(&referenceMutex);
        timing.ppsMono=ppsClock.anchorMono();timing.ppsUtc=ppsClock.anchorUtc();timing.ppsCount=ppsClock.latest.count;timing.ppsPeriod=ppsClock.period;timing.paired=ppsClock.locked(now);timing.set=selectedSet;timing.parent=parentSession;
        pthread_mutex_unlock(&referenceMutex);
        if(recording && !gpsQueue.push(timing))++gpsDrops;
        pthread_mutex_unlock(&recordingMutex);
      }
      replyDue=now+10000;replyPending=true;
    }
    if(replyPending && now>=replyDue && !linkWire.busy()) {
      sprctl::encode(response,frame);
      if(linkWire.send(frame,sizeof(frame))){replyPending=false;txDeadline=now+sprwire::frame_us+400000;}
    }
    if(txDeadline && linkWire.busy() && now>=txDeadline){linkWire.abort();txDeadline=0;resetReply=false;++linkTimeouts;}
    if(resetReply && !replyPending && !linkWire.busy()) {
      rebootRequested=true;resetReply=false;
    }
    if(lastRequest && now-lastRequest>5000000) {
      ++linkTimeouts;lastRequest=0;parser.reset();
      pthread_mutex_lock(&referenceMutex);parentReference={};pthread_mutex_unlock(&referenceMutex);
    }
    linkCrcErrors=parser.errors;
    usleep(1000); // Dedicated task; never blocks IMU or SD task.
  }
  return nullptr;
}
void reportSdSpace() {
  struct statfs info{};
  if(statfs("/mnt/sd0",&info)==0)
    Serial.printf("# SD_SPACE total_bytes=%llu free_bytes=%llu\n",
      (unsigned long long)info.f_blocks*info.f_bsize,(unsigned long long)info.f_bavail*info.f_bsize);
  else Serial.printf("# SD_SPACE_FAILED errno=%d\n",errno);
}
// Explicit USB diagnostics only: inspect the medium without mounting, writing
// or changing a partition. Useful when the filesystem cannot be mounted.
void reportSdBoot() {
  int fd=open("/dev/mmcsd0",O_RDONLY);
  if(fd<0){Serial.printf("# SD_BOOT open_errno=%d\n",errno);return;}
  uint8_t sector[512];ssize_t count=read(fd,sector,sizeof(sector));
  if(count==sizeof(sector)) {
    auto le16=[](const uint8_t* p){return unsigned(p[0])|(unsigned(p[1])<<8);};
    auto le32=[](const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);};
    uint32_t lba=le16(sector+11)==512?0:le32(sector+454);
    Serial.printf("# SD_BOOT signature=%02x%02x partition_type=%02x partition_lba=%lu\n",sector[511],sector[510],sector[450],(unsigned long)lba);
    if(lba) {
      if(lseek(fd,off_t(lba)*512,SEEK_SET)<0)count=-1;
      else count=read(fd,sector,sizeof(sector));
    }
    if(count==sizeof(sector)) {
      char oem[9];for(unsigned i=0;i<8;++i)oem[i]=sector[i+3]>=32&&sector[i+3]<=126?char(sector[i+3]):'.';oem[8]=0;
      Serial.printf("# SD_BPB oem=%s bytes_per_sector=%u sectors_per_cluster=%u fats=%u fat16_sectors=%u fat32_sectors=%lu\n",oem,le16(sector+11),sector[13],sector[16],le16(sector+22),(unsigned long)le32(sector+36));
    }
  }
  if(count!=sizeof(sector))Serial.printf("# SD_BOOT read=%ld errno=%d\n",long(count),errno);
  close(fd);
}
bool writeComplete(int fd,const char *data,size_t size) {
  uint64_t began=micros();
  off_t offset=lseek(fd,0,SEEK_CUR);
  if(offset<0)return false;
  size_t done=0;
  while(done<size) {
    size_t requested=aqsd::writeLimit(uint64_t(offset)+done,size-done);
    ssize_t result=write(fd,data+done,requested);
    if(result<0 && errno==EINTR) continue;
    if(result<=0) {
      int error=result<0 ? errno : 0;
      Serial.printf("# SD_WRITE_FAILED errno=%d result=%ld written=%u requested=%u\n",
        error,(long)result,unsigned(done),unsigned(size));
      reportSdSpace(); errno=error; return false;
    }
    done+=size_t(result);
    Watchdog.kick(); // Progress was made; only a stuck call should trigger reset.
  }
  uint64_t elapsed=micros()-began;if(elapsed>maxSdWriteUs)maxSdWriteUs=elapsed;
  return true;
}
bool flushLog(int fd,const char *name) {
  int result;
  do { result=fsync(fd); } while(result<0 && errno==EINTR);
  if(result<0) {
    int error=errno;
    Serial.printf("# SD_SYNC_FAILED file=%s errno=%d\n",name,error);
    errno=error; return false;
  }
  return true;
}
bool saveLine(int fd,const char *line,bool checksum=true) {
  size_t len=strlen(line);
  char protectedLine[420];
  if(checksum) {
    if(len && line[len-1]=='\n') --len;
    if(len+12>=sizeof(protectedLine)) return false;
    memcpy(protectedLine,line,len);
    snprintf(protectedLine+len,sizeof(protectedLine)-len,",%08lx\n",(unsigned long)aq::crc32((const uint8_t*)line,len));
    line=protectedLine; len=strlen(line);
  }
  if(fd<0 || !writeComplete(fd,line,len)) return false;
  ++sdRows; return true;
}
void discardQueuedRows() {
  ImuRow imu;
  while(imuQueue.pop(imu)) ++queueDrops;
  GpsRow gps;
  while(gpsQueue.pop(gps)) ++gpsDrops;
}
void closeLogsAfterFailure() {
  // Do not fsync known-bad descriptors. close() can still enter the driver, so
  // the watchdog remains the final escape path if the device no longer responds.
  int oldImu=imuFile, oldGps=gpsFile;
  imuFile=gpsFile=-1;
  if(oldImu>=0) close(oldImu);
  if(oldGps>=0) close(oldGps);
}
void failSd(const char *reason,int detail=0) {
  if(!recording && sdRecovery.pending) return;
  sdFailure=!strcmp(reason,"SD_SYNC")?sprctl::SD_SYNC:sprctl::SD_WRITE;sdDetail=detail;
  sdLastFailure=reason; sdLastDetail=detail; ++sdErrors; sdReady=false;
  pthread_mutex_lock(&recordingMutex); recording=false; pthread_mutex_unlock(&recordingMutex);
  closeLogsAfterFailure();
  discardQueuedRows();
  sdRecovery.failed(millis());
  Serial.printf("# RECORDING_FAILED reason=%s detail=%d retry_ms=%lu\n",reason,detail,
    (unsigned long)(sdRecovery.attempts<3 ? aq::SpresenseSdRecovery::retryDelayMs(sdRecovery.attempts) : 0));
}
void failWorker(const char *reason,int detail=0) {
  sdFailure=sprctl::SD_FORMATTER;sdDetail=detail;
  workerOk=false; workerPending=false; ++workerErrors; ++sdErrors;
  sdReady=false;
  pthread_mutex_lock(&recordingMutex); recording=false; pthread_mutex_unlock(&recordingMutex);
  sdRecovery.stop();
  closeLogsAfterFailure();
  discardQueuedRows();
  Serial.printf("# RECORDING_FAILED reason=%s detail=%d recording_stopped\n",reason,detail);
}
void serviceFormatter() {
  if(!workerOk) { ImuRow discard; while(imuQueue.pop(discard)) ++queueDrops; return; }
  aqimu::Batch* completed=nullptr;
  if(workerPending) {
    int8_t message=0; uint32_t response=0;
    int ret=MP.Recv(&message,&response,1);
    if(ret>=0) {
      __sync_synchronize();
      if(message!=11 || response!=batch->id || batch->version!=aqimu::magic || batch->error ||
         !batch->length || batch->length>sizeof(batch->csv) || batch->count>aqimu::batchSize) { failWorker("FORMATTER_RESPONSE",batch->error); return; }
      uint32_t elapsed=millis()-workerStarted;if(elapsed>maxFormatterMs)maxFormatterMs=elapsed;
      workerPending=false; ++workerBatches;
      completed=batch;
      batch=batch==batchBuffers?batchBuffers+1:batchBuffers;
    } else if(millis()-workerStarted>2000) { failWorker("FORMATTER_TIMEOUT",ret); return; }
  }
  if(!workerPending) {
    batch->count=0;
    while(batch->count<aqimu::batchSize && imuQueue.pop(batch->rows[batch->count])) ++batch->count;
    if(batch->count) {
      batch->version=aqimu::magic; batch->id=++workerId; batch->error=1; batch->length=0;
      __sync_synchronize();
      int ret=MP.Send(10,(void*)batch,1);
      if(ret<0) { failWorker("FORMATTER_SEND",ret); return; }
      workerPending=true; workerStarted=millis();
    }
  }
  // The completed buffer remains owned by MainCore. SubCore formats the other
  // buffer while SD writes this one, rather than serializing both slow steps.
  if(completed){
    if(sdReady){
      if(!writeComplete(imuFile,completed->csv,completed->length)){failSd("IMU_SD_WRITE",errno);return;}
      sdRows+=completed->count;
    }else queueDrops+=completed->count;
  }
}
bool openLogs() {
  // SDHCI.begin() only waits for /mnt/sd0; report mount failure separately
  // from exclusive-create, write, sync and readback failures. Never format.
  if(!card.begin()) return openFailed(sprctl::SD_MOUNT,errno);
  char path[64];
  int id=0;
  for(;id<100000;id++) {
    if((id&127)==0) Watchdog.kick();
    snprintf(imuPath,sizeof(imuPath),"/mnt/sd0/I%05d.CSV",id);
    snprintf(gpsPath,sizeof(gpsPath),"/mnt/sd0/T%05d.CSV",id);
    snprintf(path,sizeof(path),"/mnt/sd0/V%05d.BIN",id);
    if(access(imuPath,F_OK)!=0 && access(gpsPath,F_OK)!=0 && access(path,F_OK)!=0) break;
  }
  if(id==100000) return openFailed(sprctl::SD_NAMES,ENOSPC);
  int test=open(path,O_CREAT|O_EXCL|O_RDWR,0666);
  if(test<0) return openFailed(sprctl::SD_PROBE_OPEN,errno);
  uint8_t pattern[512], back[512];
  for(size_t i=0;i<sizeof(pattern);i++) pattern[i]=uint8_t(i*37+id);
  bool ok=write(test,pattern,sizeof(pattern))==sizeof(pattern) && fsync(test)==0;
  ok=(close(test)==0) && ok;
  test=open(path,O_RDONLY);
  ok=test>=0 && read(test,back,sizeof(back))==sizeof(back) && !memcmp(pattern,back,sizeof(back)) && ok;
  if(test>=0) close(test);
  unlink(path); // Only the exclusively created verification file.
  if(!ok) return openFailed(sprctl::SD_PROBE_IO,EIO);
  imuFile=open(imuPath,O_CREAT|O_EXCL|O_WRONLY,0666);
  gpsFile=open(gpsPath,O_CREAT|O_EXCL|O_WRONLY,0666);
  if(imuFile<0 || gpsFile<0) {
    int error=errno;
    if(imuFile>=0) close(imuFile);
    if(gpsFile>=0) close(gpsFile);
    imuFile=gpsFile=-1; return openFailed(sprctl::SD_LOG_OPEN,error);
  }
  bool headers=saveLine(imuFile,aqimu::csvHeader,false) &&
    saveLine(gpsFile,"seq,parent_reference_mono_us,utc_s,nav_usec,flags,fix_unused,satellites_unused,lat_unused,lon_unused,alt_unused,imu_samples,imu_errors,imu_gaps,sd_errors,sd_rows,parent_time_source,pps_anchor_mono_us,pps_anchor_utc_us,pps_count,pps_period_us,pps_paired,parent_set_generation,parent_session,crc32\n",false);
  if(!headers || !flushLog(imuFile,"IMU_HEADER") || !flushLog(gpsFile,"GPS_HEADER")) {
    int error=errno;closeLogsAfterFailure();return openFailed(sprctl::SD_HEADER,error);
  }
  sdSyncedRows=sdRows.load();
  return true;
}
void resumeRecording() {
  pthread_mutex_lock(&calibrationMutex);bool calibrating=calibrationSession.active();pthread_mutex_unlock(&calibrationMutex);
  if(calibrating){Serial.println("# RESUME_REFUSED calibration_active");return;}
  if(recording) { Serial.println("# RECORDING_ALREADY_ACTIVE"); return; }
  // Formatter faults still require a reboot.  SD faults may be retried while
  // preserving cumulative error counters and the parent link.
  if(!workerOk || workerErrors || workerPending ||
     imuFile>=0 || gpsFile>=0 || imuQueue.head!=imuQueue.tail || gpsQueue.head!=gpsQueue.tail) {
    Serial.println("# RESUME_REFUSED busy_or_fault"); return;
  }
  sdRecovery.manualStart();
  if(!openLogs()) {
    closeLogsAfterFailure();
    sdReady=false; ++sdErrors; sdLastFailure="MANUAL_OPEN"; sdLastDetail=errno;
    sdRecovery.failed(millis());
    Serial.println("# RESUME_FAILED SD"); return;
  }
  pthread_mutex_lock(&recordingMutex);
  sdReady=true; recording=true;safelyStopped=false;
  pthread_mutex_unlock(&recordingMutex);
  sdRecovery.mounted(millis());
  Serial.println("# RECORDING_RESUMED new_files parent_link_unchanged");
  Serial.println(imuPath); Serial.println(gpsPath);
}
void serviceSdRecovery() {
  uint32_t now=millis();
  if(sdReady) { sdRecovery.healthy(now); return; }
  if(!sdRecovery.due(now) || !workerOk || workerPending) return;
  sdRecovery.attempting(now);
  Serial.printf("# SD_RECOVERY_ATTEMPT attempt=%lu\n",(unsigned long)sdRecovery.attempts);
  if(openLogs()) {
    pthread_mutex_lock(&recordingMutex);
    sdReady=true; recording=true;safelyStopped=false;
    pthread_mutex_unlock(&recordingMutex);
    sdRecovery.mounted(millis());
    Serial.println("# SD_RECOVERED new_files parent_link_unchanged");
    Serial.println(imuPath); Serial.println(gpsPath);
    return;
  }
  closeLogsAfterFailure();
  ++sdErrors; sdLastFailure="RECOVERY_OPEN"; sdLastDetail=errno;
  sdRecovery.failed(millis());
  Serial.printf("# SD_RECOVERY_FAILED attempt=%lu exhausted=%u\n",
    (unsigned long)sdRecovery.attempts,unsigned(sdRecovery.exhausted()));
}
uint32_t scanCalibration(sprcal::Coefficients* restored=nullptr) {
  DIR* directory=opendir("/mnt/sd0");if(!directory){calibrationRestoreError=1;return 0;}
  uint32_t highestName=0,highestValid=0;dirent* entry;
  while((entry=readdir(directory))) {
    unsigned number=0;char extra=0;
    if(strlen(entry->d_name)!=10 || entry->d_name[0]!='K' || entry->d_name[6]!='.')continue;
    bool calibrationFile=!strcmp(entry->d_name+7,"CAL"),temporaryFile=!strcmp(entry->d_name+7,"TMP");
    if((!calibrationFile && !temporaryFile) || sscanf(entry->d_name+1,"%5u%c",&number,&extra)!=2 || extra!='.' || !number)continue;
    if(number>highestName)highestName=number;
    if(!restored || !calibrationFile)continue;
    char path[64];snprintf(path,sizeof(path),"/mnt/sd0/%s",entry->d_name);
    int fd=open(path,O_RDONLY);if(fd<0){calibrationRestoreError=1;continue;}
    uint8_t bytes[sprcal::recordSize],extraByte;sprcal::Coefficients value;
    bool ok=read(fd,bytes,sizeof(bytes))==sizeof(bytes) && read(fd,&extraByte,1)==0;
    close(fd);ok=ok && sprcal::decode(bytes,value) && value.generation==number;
    if(!ok){calibrationRestoreError=1;continue;}
    if(number>highestValid){highestValid=number;*restored=value;}
  }
  closedir(directory);return highestName;
}
bool persistCalibration(sprcal::Coefficients& value) {
  uint32_t highest=scanCalibration();if(highest>=99999 || !sprcal::valid(value))return false;
  value.generation=highest+1;char path[64],committed[64];
  snprintf(path,sizeof(path),"/mnt/sd0/K%05lu.TMP",(unsigned long)value.generation);
  snprintf(committed,sizeof(committed),"/mnt/sd0/K%05lu.CAL",(unsigned long)value.generation);
  uint8_t bytes[sprcal::recordSize],back[sprcal::recordSize];sprcal::encode(value,bytes);
  int fd=open(path,O_CREAT|O_EXCL|O_WRONLY,0666);if(fd<0)return false;
  bool ok=writeComplete(fd,(const char*)bytes,sizeof(bytes));
  bool synced=fsync(fd)==0,closed=close(fd)==0;ok=ok && synced && closed;
  fd=open(path,O_RDONLY);if(fd<0)return false;
  uint8_t extra;sprcal::Coefficients checked;
  bool readback=read(fd,back,sizeof(back))==sizeof(back) && read(fd,&extra,1)==0;
  closed=close(fd)==0;
  if(!(ok && readback && closed && !memcmp(bytes,back,sizeof(bytes)) && sprcal::decode(back,checked)))return false;
  // Only publish a readback-verified new record. Failed .TMP files are preserved
  // and ignored during restoration. Never replace an existing calibration file.
  struct stat info{};if(stat(committed,&info)==0 || errno!=ENOENT)return false;
  return rename(path,committed)==0;
}
void calibrationOperation(uint8_t op) {
  pthread_mutex_lock(&calibrationMutex);
  sprcal::Coefficients next=op==sprctl::CAL_SAVE?calibrationSession.candidate:appliedCalibration;
  if(op==sprctl::CAL_CLEAR)next.enabled=false;
  pthread_mutex_unlock(&calibrationMutex);
  bool ok=!recording && safelyStopped && persistCalibration(next);
  pthread_mutex_lock(&calibrationMutex);
  calibrationSaveError=ok?0:1;
  if(ok){appliedCalibration=next;calibrationSession.end();calibrationSession.state=op==sprctl::CAL_SAVE?sprcal::SAVED:sprcal::NONE;}
  lastOperation=op;lastOperationResult=ok?sprctl::OK:sprctl::FAILED;operationBusy=false;
  pthread_mutex_unlock(&calibrationMutex);
  Serial.printf("# CALIBRATION op=%u result=%u generation=%lu enabled=%u\n",unsigned(op),unsigned(lastOperationResult.load()),(unsigned long)appliedCalibration.generation,unsigned(appliedCalibration.enabled));
}
bool calibrationMaintenance(const char* line){
 bool enabling=!strcmp(line,"CAL ENABLE_SAVED");char file[16]{};unsigned expected=0;int consumed=0;
 bool deleting=sscanf(line,"CAL DELETE_DISABLED %15s %x%n",file,&expected,&consumed)==2&&line[consumed]==0;
 if(!enabling&&!deleting){Serial.println("# CAL_MAINTENANCE refused=command");return false;}
 bool empty=false;if(!operationBusy.compare_exchange_strong(empty,true)){Serial.println("# CAL_MAINTENANCE refused=busy");return false;}
 pthread_mutex_lock(&calibrationMutex);auto current=appliedCalibration;bool idle=!calibrationSession.active();pthread_mutex_unlock(&calibrationMutex);
 bool ok=!recording&&safelyStopped&&idle&&current.generation&&sprcal::valid(current);
 if(enabling&&ok){
  auto next=current;next.enabled=true;ok=persistCalibration(next);
  if(ok){pthread_mutex_lock(&calibrationMutex);appliedCalibration=next;calibrationSession.end();calibrationSession.state=sprcal::SAVED;calibrationSaveError=0;pthread_mutex_unlock(&calibrationMutex);}
 }else if(deleting&&ok){
  unsigned number=0;char extra=0;ok=current.enabled&&strlen(file)==10&&file[0]=='K'&&!strcmp(file+6,".CAL")&&sscanf(file+1,"%5u%c",&number,&extra)==2&&extra=='.'&&number&&number<current.generation;
  char path[64];snprintf(path,sizeof(path),"/mnt/sd0/%s",file);uint8_t bytes[sprcal::recordSize],back[sprcal::recordSize],tail;sprcal::Coefficients old;
  if(ok){int fd=open(path,O_RDONLY);ok=fd>=0&&read(fd,bytes,sizeof(bytes))==sizeof(bytes)&&read(fd,&tail,1)==0;if(fd>=0)ok=(close(fd)==0)&&ok;}
  if(ok)ok=sprcal::decode(bytes,old)&&old.generation==number&&!old.enabled&&spnav::get32(bytes+60)==expected;
  if(ok){old.enabled=true;old.generation=current.generation;sprcal::encode(old,back);sprcal::encode(current,bytes);ok=!memcmp(bytes,back,sizeof(bytes));}
  if(ok){struct stat info{};ok=unlink(path)==0&&stat(path,&info)<0&&errno==ENOENT;}
 }
 operationBusy=false;
 Serial.printf("# CAL_MAINTENANCE action=%s ok=%u generation=%lu enabled=%u\n",enabling?"ENABLE_SAVED":"DELETE_DISABLED",unsigned(ok),(unsigned long)appliedCalibration.generation,unsigned(appliedCalibration.enabled));return ok;
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
    Watchdog.kick();
  }
  if(count<0) ok=false;
  if(close(fd)!=0) ok=false;
  Serial.println(ok ? "# EXPORT_END" : "# EXPORT_FAILED");
}
void setup() {
  Serial.begin(115200);
  LowPower.begin();
  unsigned previousClock=unsigned(LowPower.getClockMode());
  LowPower.clockMode(CLOCK_MODE_156MHz);
  Serial.printf("# CPU_CLOCK previous_mode=%u mode=%u high_mode=0\n",previousClock,unsigned(LowPower.getClockMode()));
  ledOff(pins::gpsReceivedLed);
  pinMode(pins::data,INPUT);
  board_gpio_write(pins::dataGpio,-1);
  pinMode(pins::pps,INPUT);attachInterrupt(digitalPinToInterrupt(pins::pps),ppsInterrupt,RISING);
  attachTimerInterrupt(linkTimer,sprwire::tick_us);
  Serial.println("# IMU_LOG schema=3 parent_utc=reference_only pps_input=D02 sample_time=pps_affine_estimate timebase=rtc_power_on_us");
  int mpResult=MP.begin(1);
  if(mpResult>=0) batchBuffers=static_cast<aqimu::Batch*>(MP.AllocSharedMemory(2*sizeof(aqimu::Batch)));
  batch=batchBuffers;
  workerOk=mpResult>=0 && batch;
  MP.RecvTimeout(MP_RECV_POLLING);
  Serial.printf("# FORMATTER subcore=1 ready=%u result=%d rate_hz=%lu shared_bytes=%u\n",workerOk,mpResult,(unsigned long)aqimu::rate,2*sizeof(aqimu::Batch));
  if(!workerOk) { ++workerErrors; recording=false; }
  // Arm before the first SD probe as a damaged card can also wedge at boot.
  Watchdog.begin();
  Watchdog.start(watchdogTimeoutMs);
  Serial.printf("# WATCHDOG timeout_ms=%lu sd_block_reboot=enabled\n",(unsigned long)watchdogTimeoutMs);
  // Service master requests even while SD mount/probe blocks during startup.
  if(!startTask(linkTask,120)) Serial.println("# LINK_THREAD_FAILED");
  sdReady=openLogs(); if(!sdReady) ++sdErrors;
  recording=sdReady && workerOk; // Never claim recording or fill the queue without an open log.
  if(sdReady) sdRecovery.mounted(millis()); else { sdLastFailure="BOOT_OPEN"; sdLastDetail=errno; sdRecovery.failed(millis()); }
  Serial.println(sdReady ? "# SD_READBACK_OK" : "# SD_FAILED");
  reportSdSpace();
  Serial.println(imuPath); Serial.println(gpsPath);
  sprcal::Coefficients restoredCalibration;scanCalibration(&restoredCalibration);
  pthread_mutex_lock(&calibrationMutex);appliedCalibration=restoredCalibration;pthread_mutex_unlock(&calibrationMutex);
  Serial.printf("# CAL_RESTORE generation=%lu enabled=%u error=%u\n",(unsigned long)appliedCalibration.generation,unsigned(appliedCalibration.enabled),unsigned(calibrationRestoreError.load()));
  if(!startTask(imuTask,150)) ++imuErrors;
}
void loop() {
  static uint32_t lastFlush=0, lastStatus=0;
  serviceGpsLed();
  uint8_t op=pendingOperation.exchange(0);
  if(op==sprctl::STOP) {
    sdRecovery.stop();pthread_mutex_lock(&recordingMutex);recording=false;pthread_mutex_unlock(&recordingMutex);
    lastOperation=op;lastOperationResult=sprctl::ACCEPTED;
  } else if(op==sprctl::START) {
    resumeRecording();lastOperation=op;lastOperationResult=recording?sprctl::OK:sprctl::FAILED;operationBusy=false;
  } else if(op==sprctl::CAL_SAVE || op==sprctl::CAL_CLEAR) {
    calibrationOperation(op);
  }
  if(rebootRequested && safelyStopped) {Serial.println("# LINK_RESET safely_stopped");LowPower.reboot();}
  char line[360];
  GpsRow gps;
  while(gpsQueue.pop(gps)) {
    aq::Nav &n=gps.nav;
    snprintf(line,sizeof(line),"%lu,%llu,%llu,%lu,%u,%u,%u,%ld,%ld,%ld,%lu,%lu,%lu,%lu,%lu,%u,%llu,%llu,%lu,%lu,%u,%lu,%lu\n",
      (unsigned long)n.sequence,(unsigned long long)gps.received_us,(unsigned long long)n.utc_s,(unsigned long)n.usec,n.flags,n.fix,n.satellites,
      (long)n.lat_e7,(long)n.lon_e7,(long)n.altitude_mm,(unsigned long)n.imu_samples,(unsigned long)n.imu_errors,(unsigned long)n.imu_gaps,(unsigned long)n.sd_errors,(unsigned long)n.sd_rows,unsigned(gps.source),(unsigned long long)gps.ppsMono,(unsigned long long)gps.ppsUtc,(unsigned long)gps.ppsCount,(unsigned long)gps.ppsPeriod,unsigned(gps.paired),(unsigned long)gps.set,(unsigned long)gps.parent);
    if(sdReady && !saveLine(gpsFile,line)) { failSd("GPS_SD_WRITE",errno); break; }
  }
  serviceFormatter();
  if(sdReady && millis()-lastFlush>=1000) {
    lastFlush=millis();
    uint32_t rowsBeforeSync=sdRows;
    if(!flushLog(imuFile,"IMU") || !flushLog(gpsFile,"GPS")) {
      failSd("SD_SYNC",errno);
    } else sdSyncedRows=rowsBeforeSync;
  }
  if(Serial.available()) {
    int c=Serial.read();
    static char maintenanceLine[100];static unsigned maintenanceUsed=0;static bool maintenanceInput=false,maintenanceOverflow=false;
    if(c=='@'&&!maintenanceInput){maintenanceInput=true;maintenanceUsed=0;maintenanceOverflow=false;}
    else if(maintenanceInput){
      if(c=='\n'){maintenanceLine[maintenanceUsed]=0;if(!maintenanceOverflow)calibrationMaintenance(maintenanceLine);else Serial.println("# CAL_MAINTENANCE refused=length");maintenanceInput=false;}
      else if(c!='\r'){if(maintenanceUsed+1<sizeof(maintenanceLine))maintenanceLine[maintenanceUsed++]=c;else maintenanceOverflow=true;}
    }else {
    if(c=='q') {
      // Share the guarded STOP path with parent controls, including an already
      // failed/closed SD. A completed stop must be observable over USB as well.
      if(!operationBusy.exchange(true))pendingOperation=sprctl::STOP;
    }
    if(c=='i') exportStoppedLog(imuPath);
    if(c=='g') exportStoppedLog(gpsPath);
    if(c=='r') resumeRecording();
    if(c=='v') {reportSdSpace();reportSdBoot();}
    if(c=='c') {
      // Read-only evidence: coefficient files, CRC and enable state, no reload/apply.
      DIR* directory=opendir("/mnt/sd0");dirent* entry;
      if(directory){while((entry=readdir(directory))){
        if(strlen(entry->d_name)!=10||entry->d_name[0]!='K'||strcmp(entry->d_name+6,".CAL"))continue;
        char path[64];snprintf(path,sizeof(path),"/mnt/sd0/%s",entry->d_name);int fd=open(path,O_RDONLY);if(fd<0)continue;
        uint8_t bytes[sprcal::recordSize],extra;sprcal::Coefficients value;
        bool readback=read(fd,bytes,sizeof(bytes))==sizeof(bytes)&&read(fd,&extra,1)==0;close(fd);
        bool valid=readback&&sprcal::decode(bytes,value);
        Serial.printf("# CAL_FILE name=%s valid=%u generation=%lu enabled=%u hex=",entry->d_name,unsigned(valid),(unsigned long)(valid?value.generation:0),unsigned(valid&&value.enabled));
        if(readback)for(auto x:bytes)Serial.printf("%02x",unsigned(x));Serial.println();
      }closedir(directory);}Serial.println("# CAL_FILES_END");
    }
    }
  }
  if(!recording && !workerPending && imuQueue.head==imuQueue.tail && gpsQueue.head==gpsQueue.tail && imuFile>=0) {
    bool imuFlushed=flushLog(imuFile,"IMU"), gpsFlushed=flushLog(gpsFile,"GPS");
    bool ok=imuFlushed && gpsFlushed && workerErrors==0; // Historical SD faults do not invalidate a successful stop.
    bool imuClosed=close(imuFile)==0,gpsClosed=close(gpsFile)==0;
    ok=imuClosed && gpsClosed && ok;
    imuFile=gpsFile=-1; sdReady=false; if(!ok) ++sdErrors;
    safelyStopped=imuFlushed && gpsFlushed && imuClosed && gpsClosed;
    if(safelyStopped)sdSyncedRows=sdRows.load();
    if(lastOperation==sprctl::STOP){lastOperationResult=safelyStopped?sprctl::OK:sprctl::FAILED;operationBusy=false;}
    Serial.println(ok ? "# STOPPED_REMOVE_SD" : "# STOP_FAILED");
  }
  if(operationBusy && lastOperation==sprctl::STOP && !recording && !workerPending && imuFile<0 && gpsFile<0 && imuQueue.head==imuQueue.tail && gpsQueue.head==gpsQueue.tail) {
    lastOperationResult=sprctl::OK;safelyStopped=true;operationBusy=false;
    Serial.println("# STOPPED_REMOVE_SD");
  }
  serviceSdRecovery();
  publishSdState();
  if(millis()-lastStatus>=1000) {
    lastStatus=millis();
    snprintf(line,sizeof(line),"# imu=%lu imu_errors=%lu gaps=%lu queue_drops=%lu gps_drops=%lu sd=%d sd_rows=%lu sd_errors=%lu",
      (unsigned long)imuSamples.load(),(unsigned long)imuErrors.load(),(unsigned long)imuGaps.load(),(unsigned long)queueDrops.load(),
      (unsigned long)gpsDrops.load(),int(sdReady.load()),(unsigned long)sdRows.load(),(unsigned long)sdErrors.load());
    Serial.println(line);
    bool gpioInput=false,gpioOutput=false,gpioDrive=false;int gpioPull=0;
    int gpioMode=board_gpio_status(pins::dataGpio,&gpioInput,&gpioOutput,&gpioDrive,&gpioPull);
    Serial.printf("# LINK_GPIO level=%u input=%u output=%u pull=%d mode=%d tx_busy=%u edges=%lu bytes=%lu\n",unsigned(board_gpio_read(pins::dataGpio)),unsigned(gpioInput),unsigned(gpioOutput),gpioPull,gpioMode,unsigned(linkWire.busy()),(unsigned long)linkEdges.load(),(unsigned long)linkBytes.load());
    Serial.printf("# recording=%u sd_state=%s\n",unsigned(recording.load()),
      sprctl::sdStateName(sdState));
    Serial.printf("# SD_DIAGNOSTIC stage=%s errno=%ld synced_rows=%lu\n",sprctl::sdFailureName(sdFailure),long(sdDetail), (unsigned long)sdSyncedRows.load());
    Serial.printf("# sd_recovery wanted=%u pending=%u exhausted=%u attempts=%lu recoveries=%lu last_failure=%s detail=%d\n",
      unsigned(sdRecovery.wanted),unsigned(sdRecovery.pending),unsigned(sdRecovery.exhausted()),
      (unsigned long)sdRecovery.attempts,(unsigned long)sdRecovery.recoveries,sdLastFailure,sdLastDetail);
    Serial.printf("# SD_PERFORMANCE max_write_us=%llu max_formatter_ms=%lu\n",(unsigned long long)maxSdWriteUs,(unsigned long)maxFormatterMs);
    Serial.printf("# formatter_batches=%lu formatter_errors=%lu pending=%u rate_hz=%lu\n",(unsigned long)workerBatches,(unsigned long)workerErrors,workerPending,(unsigned long)aqimu::rate);
    pthread_mutex_lock(&referenceMutex);auto ref=parentReference;auto pc=ppsClock;auto timing=latestTimedRow;pthread_mutex_unlock(&referenceMutex);
    Serial.printf("# IMU_TIME seq=%lu received_us=%llu raw=%lu sample_mono_us=%llu sample_utc_us=%llu quality=%u clock_generation=%lu set=%lu parent_session=%lu uncertainty_us=%lu\n",
      (unsigned long)timing.seq,(unsigned long long)timing.received_us,(unsigned long)timing.data.timestamp,
      (unsigned long long)timing.sample_mono_us,(unsigned long long)timing.sample_utc_us,unsigned(timing.time_quality),
      (unsigned long)timing.clock_generation,(unsigned long)timing.set_generation,(unsigned long)timing.parent_session,(unsigned long)UINT32_MAX);
    Serial.printf("# PPS count=%lu period_us=%lu fresh=%u paired=%u imu_time_ready=%u drops=%lu accuracy_verified=0\n",(unsigned long)pc.latest.count,(unsigned long)pc.period,unsigned(pc.fresh(monoUs())),unsigned(pc.locked(monoUs())),unsigned(imuTimeReady.load()),(unsigned long)ppsDrops.load());
    Serial.printf("# LINK packets=%lu crc=%lu framing=%lu overflow=%lu collisions=%lu timer_ticks=%lu utc_valid=%u utc_source=%u pps_input=D02 last_op=%u result=%u\n",
      (unsigned long)linkPackets.load(),(unsigned long)linkCrcErrors.load(),(unsigned long)linkWire.framing.load(),
      (unsigned long)linkWire.overflow.load(),(unsigned long)linkWire.collisions.load(),(unsigned long)timerTicks.load(),
      unsigned(ref.valid(monoUs())),unsigned(ref.source),unsigned(lastOperation.load()),unsigned(lastOperationResult.load()));
  }
  Watchdog.kick();
  usleep(1000); // Yield MainCore task; sensor poll and worker run independently.
}
