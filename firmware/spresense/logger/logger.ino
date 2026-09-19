#include <Arduino.h>
#include <GNSS.h>
#include <SDHCI.h>
#include <AquaBeacon.h>
#include <nuttx/sensors/cxd5602pwbimu.h>
#include <arch/board/cxd56_cxd5602pwbimu.h>
#include <pthread.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <math.h>
#include <atomic>

// Sony CXD5602PWBIMU Add-on, official Arduino core 3.4.7.
// SD I/O runs only in loop(); GNSS/UART and IMU acquisition have dedicated tasks.
SDClass card;
SpGnss gnss;
struct ImuRow { uint64_t received_us; uint32_t seq; cxd5602pwbimu_data_t data; };
struct GpsRow { uint64_t received_us; aq::Nav nav; };
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
std::atomic<bool> imuReady{false}, sdReady{false}, recording{true};
pthread_mutex_t recordingMutex=PTHREAD_MUTEX_INITIALIZER;
int imuFile=-1, gpsFile=-1;
char imuPath[64], gpsPath[64];

uint64_t monoUs() {
  timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
  return uint64_t(t.tv_sec)*1000000ULL+t.tv_nsec/1000;
}
void *imuTask(void *) {
  if(board_cxd5602pwbimu_initialize(5)<0) { ++imuErrors; return nullptr; }
  int fd=open("/dev/imu0",O_RDONLY|O_NONBLOCK);
  cxd5602pwbimu_range_t range{4,500};
  if(fd<0) { ++imuErrors; return nullptr; }
  if(ioctl(fd,SNIOC_SSAMPRATE,120) || ioctl(fd,SNIOC_SDRANGE,(unsigned long)(uintptr_t)&range)
     || ioctl(fd,SNIOC_SFIFOTHRESH,1) || ioctl(fd,SNIOC_ENABLE,1)) {
    ++imuErrors; close(fd); return nullptr;
  }
  imuReady=true;
  uint32_t previous=0; bool havePrevious=false;
  uint32_t expectedStep=0;
  while(true) {
    pollfd p{fd,POLLIN,0}; int ready=poll(&p,1,1000);
    if(ready<0 && errno==EINTR) continue;
    if(ready<=0 || !(p.revents&POLLIN)) { ++imuErrors; imuReady=false; continue; }
    ImuRow row{};
    ssize_t n=read(fd,&row.data,sizeof(row.data));
    if(n<0 && (errno==EAGAIN || errno==EINTR)) continue;
    if(n!=sizeof(row.data)) { ++imuErrors; continue; }
    row.received_us=monoUs(); row.seq=++imuSamples; imuReady=true;
    // Preserve hardware timestamp verbatim. Detect discontinuity against the
    // smallest observed nonzero step; units are deliberately not guessed.
    if(havePrevious) {
      uint32_t step=row.data.timestamp-previous;
      if(step && (!expectedStep || step<expectedStep)) expectedStep=step;
      if(!step || (expectedStep && step>expectedStep+expectedStep/2)) ++imuGaps;
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
  if(gnss.begin()!=0 || gnss.select(GPS)!=0 || gnss.setInterval(1L)!=0 || gnss.start(COLD_START)!=0) {
    Serial.println("# GNSS_START_FAILED"); return nullptr;
  }
  gnss.start1PPS(); // Hardware GNSS output on D02, never software generated.
  aq::Nav out;
  while(true) {
    if(!gnss.waitUpdate(1)) continue;
    SpNavData nav{}; gnss.getNavData(&nav);
    uint64_t received=monoUs();
    out.sequence++; out.flags=0; out.fix=nav.posFixMode; out.satellites=nav.numSatellitesCalcPos;
    out.utc_s=aq::unix_seconds(nav.time.year,nav.time.month,nav.time.day,nav.time.hour,nav.time.minute,nav.time.sec);
    out.usec=nav.time.usec;
    bool fix=nav.type==SpPvtTypeGnss && nav.posDataExist && (nav.posFixMode==Fix2D || nav.posFixMode==Fix3D);
    if(fix && out.utc_s && out.usec<=1000) out.flags|=aq::time_valid;
    if(fix && isfinite(nav.latitude) && isfinite(nav.longitude) && isfinite(nav.altitude)
       && fabs(nav.latitude)<=90 && fabs(nav.longitude)<=180 && fabs(nav.altitude)<2000000) {
      out.flags|=aq::position_valid;
      out.lat_e7=lround(nav.latitude*1e7); out.lon_e7=lround(nav.longitude*1e7); out.altitude_mm=lround(nav.altitude*1000);
    } else { out.lat_e7=out.lon_e7=out.altitude_mm=0; }
    if(sdReady) out.flags|=aq::sd_ok;
    if(imuReady) out.flags|=aq::imu_ok;
    out.imu_samples=imuSamples; out.imu_errors=imuErrors; out.imu_gaps=imuGaps.load()+queueDrops.load();
    out.sd_errors=sdErrors.load()+gpsDrops.load(); out.sd_rows=sdRows; out.uptime_ms=uint32_t(received/1000);
    uint8_t frame[aq::frame_size]; aq::encode(out,frame);
    Serial2.write(frame,sizeof(frame)); // Send before any SD operation.
    pthread_mutex_lock(&recordingMutex);
    if(recording && !gpsQueue.push(GpsRow{received,out})) ++gpsDrops;
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
bool openLogs() {
  if(!card.begin()) return false;
  int id=0;
  for(;id<100000;id++) {
    snprintf(imuPath,sizeof(imuPath),"/mnt/sd0/I%05d.CSV",id);
    snprintf(gpsPath,sizeof(gpsPath),"/mnt/sd0/G%05d.CSV",id);
    if(access(imuPath,F_OK)!=0 && access(gpsPath,F_OK)!=0) break;
  }
  if(id==100000) return false;
  char path[64]; snprintf(path,sizeof(path),"/mnt/sd0/V%05d.BIN",id);
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
  if(imuFile<0 || gpsFile<0) return false;
  return saveLine(imuFile,"seq,received_mono_us,sensor_timestamp_raw,temp,gx,gy,gz,ax,ay,az,crc32\n",false) &&
    saveLine(gpsFile,"seq,received_mono_us,utc_s,nav_usec,flags,fix,satellites,lat_e7,lon_e7,altitude_mm,imu_samples,imu_errors,imu_gaps,sd_errors,sd_rows,crc32\n",false);
}
bool startTask(void *(*fn)(void *)) {
  pthread_attr_t a; pthread_attr_init(&a); pthread_attr_setstacksize(&a,16384);
  pthread_t t; int r=pthread_create(&t,&a,fn,nullptr); pthread_attr_destroy(&a);
  if(!r) pthread_detach(t); return r==0;
}
void setup() {
  Serial.begin(115200);
  sdReady=openLogs(); if(!sdReady) ++sdErrors;
  Serial.println(sdReady ? "# SD_READBACK_OK" : "# SD_FAILED");
  Serial.println(imuPath); Serial.println(gpsPath);
  if(!startTask(imuTask)) ++imuErrors;
  if(!startTask(gpsTask)) Serial.println("# GNSS_THREAD_FAILED");
}
void loop() {
  static uint32_t lastFlush=0, lastStatus=0;
  char line[360];
  GpsRow gps;
  while(gpsQueue.pop(gps)) {
    aq::Nav &n=gps.nav;
    snprintf(line,sizeof(line),"%lu,%llu,%llu,%lu,%u,%u,%u,%ld,%ld,%ld,%lu,%lu,%lu,%lu,%lu\n",
      (unsigned long)n.sequence,(unsigned long long)gps.received_us,(unsigned long long)n.utc_s,(unsigned long)n.usec,n.flags,n.fix,n.satellites,
      (long)n.lat_e7,(long)n.lon_e7,(long)n.altitude_mm,(unsigned long)n.imu_samples,(unsigned long)n.imu_errors,(unsigned long)n.imu_gaps,(unsigned long)n.sd_errors,(unsigned long)n.sd_rows);
    if(sdReady) saveLine(gpsFile,line);
  }
  ImuRow row;
  for(int i=0;i<32 && imuQueue.pop(row);i++) {
    auto &d=row.data;
    snprintf(line,sizeof(line),"%lu,%llu,%lu,%.6f,%.8f,%.8f,%.8f,%.8f,%.8f,%.8f\n",
      (unsigned long)row.seq,(unsigned long long)row.received_us,(unsigned long)d.timestamp,d.temp,d.gx,d.gy,d.gz,d.ax,d.ay,d.az);
    if(sdReady) saveLine(imuFile,line);
  }
  if(sdReady && millis()-lastFlush>=1000) {
    lastFlush=millis();
    if(fsync(imuFile)!=0 || fsync(gpsFile)!=0) { ++sdErrors; sdReady=false; }
  }
  if(Serial.available() && Serial.read()=='q') {
    pthread_mutex_lock(&recordingMutex);
    recording=false;
    pthread_mutex_unlock(&recordingMutex);
    // Finish draining queues before closing on a subsequent loop.
  }
  if(!recording && imuQueue.head==imuQueue.tail && gpsQueue.head==gpsQueue.tail && imuFile>=0) {
    bool ok=fsync(imuFile)==0 && fsync(gpsFile)==0;
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
  }
  delay(1);
}
