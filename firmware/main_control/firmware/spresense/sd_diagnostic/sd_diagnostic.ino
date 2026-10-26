#include <Arduino.h>
#include <SDHCI.h>
#include <LowPower.h>
#include <Watchdog.h>
#include <SpresenseControl.h>
#include <arch/board/board.h>
#include <arch/chip/pin.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/statfs.h>
#include <nuttx/sensors/cxd5602pwbimu.h>
#include <arch/board/cxd56_cxd5602pwbimu.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <poll.h>
#include <atomic>
#include <sd_sector_write.h>
// Standalone diagnostic. Exclusively create new files; never format or remove.
// No parent USB/GNSS access. D03 is input only; no PPS output or link responses.
SDClass card;
alignas(32) uint8_t buffer[8192],back[8192];
sprwire::Uart wire;
volatile uint32_t ticks=0;
unsigned tick(){wire.tick(board_gpio_read(PIN_PWM3));++ticks;return sprwire::tick_us;}
uint32_t u32(const uint8_t* b){return uint32_t(b[0])|uint32_t(b[1])<<8|uint32_t(b[2])<<16|uint32_t(b[3])<<24;}
uint16_t u16(const uint8_t* b){return uint16_t(b[0])|uint16_t(b[1])<<8;}
void inspect(){
 struct statfs info{};int ret=statfs("/mnt/sd0",&info);
 Serial.printf("# FS ret=%d errno=%d type=%lx block=%lu total=%llu free=%llu\n",ret,ret<0?errno:0,(unsigned long)info.f_type,(unsigned long)info.f_bsize,(unsigned long long)info.f_blocks*info.f_bsize,(unsigned long long)info.f_bavail*info.f_bsize);
 int fd=open("/dev/mmcsd0",O_RDONLY);if(fd<0){Serial.printf("# BOOT open_errno=%d\n",errno);return;}
 ssize_t n=read(fd,buffer,512);
 if(n==512){
  Serial.printf("# MBR signature=%02x%02x type=%02x partition_lba=%lu\n",buffer[511],buffer[510],buffer[450],(unsigned long)u32(buffer+454));
  uint32_t lba=(u16(buffer+11)==512)?0:u32(buffer+454);
  if(lba && lseek(fd,off_t(lba)*512,SEEK_SET)>=0)n=read(fd,buffer,512);
  if(n==512)Serial.printf("# BPB lba=%lu bytes_per_sector=%u sectors_per_cluster=%u reserved=%u fats=%u root_entries=%u total_sectors=%lu fat16_sectors=%u fat32_sectors=%lu label16=%.8s label32=%.8s\n",(unsigned long)lba,u16(buffer+11),buffer[13],u16(buffer+14),buffer[16],u16(buffer+17),(unsigned long)(u16(buffer+19)?u16(buffer+19):u32(buffer+32)),u16(buffer+22),(unsigned long)u32(buffer+36),buffer+54,buffer+82);
 }else Serial.printf("# BOOT read=%ld errno=%d\n",(long)n,errno);
 close(fd);
}
std::atomic<uint32_t> imuCount{0},imuErrors{0};
bool imuStarted=false;
void* acquire(void*){
 if(board_cxd5602pwbimu_initialize(5)<0){++imuErrors;return nullptr;}
 int fd=open("/dev/imu0",O_RDONLY|O_NONBLOCK);cxd5602pwbimu_range_t range{4,500};
 if(fd<0 || ioctl(fd,SNIOC_SSAMPRATE,960) || ioctl(fd,SNIOC_SDRANGE,(unsigned long)(uintptr_t)&range) || ioctl(fd,SNIOC_SFIFOTHRESH,1) || ioctl(fd,SNIOC_ENABLE,1)){++imuErrors;return nullptr;}
 while(true){pollfd p{fd,POLLIN,0};int ready=poll(&p,1,1000);
  if(ready<0 && errno==EINTR)continue;
  if(ready<=0 || !(p.revents&POLLIN)){++imuErrors;continue;}
  cxd5602pwbimu_data_t row;ssize_t n=read(fd,&row,sizeof(row));
  if(n==sizeof(row))++imuCount;else if(errno!=EAGAIN && errno!=EINTR)++imuErrors;
 }return nullptr;
}
void startImu(){if(imuStarted)return;imuStarted=true;
 pthread_attr_t a;pthread_attr_init(&a);pthread_attr_setstacksize(&a,16384);
 sched_param p{};p.sched_priority=150;pthread_attr_setinheritsched(&a,PTHREAD_EXPLICIT_SCHED);pthread_attr_setschedpolicy(&a,SCHED_FIFO);pthread_attr_setschedparam(&a,&p);
 pthread_t t;int r=pthread_create(&t,&a,acquire,nullptr);pthread_attr_destroy(&a);
 if(!r)pthread_detach(t);Serial.printf("# IMU_DIAGNOSTIC start=%d\n",r);
}
void phase(size_t chunk,bool timer,bool bounded=false){
 if(timer)attachTimerInterrupt(tick,sprwire::tick_us);
 char path[64];int fd=-1;
 for(unsigned id=0;id<100000;id++){snprintf(path,sizeof(path),"/mnt/sd0/D%05u.BIN",id);fd=open(path,O_CREAT|O_EXCL|O_RDWR,0666);if(fd>=0||errno!=EEXIST)break;Watchdog.kick();}
 if(fd<0){Serial.printf("# PHASE_OPEN_FAILED errno=%d\n",errno);if(timer)detachTimerInterrupt();return;}
 constexpr size_t total=4*1024*1024;
 size_t done=0;uint32_t writes=0;uint64_t start=micros(),maxUs=0;int error=0;
 Serial.printf("# PHASE_BEGIN file=%s chunk=%u timer=%u target_bytes=%u target_bps=200000 bounded=%u\n",path,unsigned(chunk),unsigned(timer),unsigned(total),unsigned(bounded));
 while(done<total){
  size_t count=min(chunk,total-done);
  for(size_t i=0;i<count;i++)buffer[i]=uint8_t((done+i)*37+11);
  uint64_t before=micros();ssize_t n=0;
  if(bounded){size_t part=0;while(part<count){size_t request=aqsd::writeLimit(done+part,count-part);ssize_t w=write(fd,buffer+part,request);if(w<=0){n=w;break;}part+=w;Watchdog.kick();}if(part==count)n=part;}
  else n=write(fd,buffer,count);uint64_t elapsed=micros()-before;if(elapsed>maxUs)maxUs=elapsed;
  if(n!=ssize_t(count)){error=n<0?errno:EIO;break;}
  done+=count;++writes;Watchdog.kick();
  uint64_t due=start+uint64_t(done)*1000000/200000;
  uint64_t now=micros();if(due>now)usleep(due-now);
 }
 bool synced=fsync(fd)==0;if(!synced && !error)error=errno;
 uint64_t writeUs=micros()-start;
 bool match=lseek(fd,0,SEEK_SET)==0;size_t checked=0;
 while(match && checked<done){
  size_t count=min(sizeof(back),done-checked);ssize_t n=read(fd,back,count);if(n!=ssize_t(count)){match=false;break;}
  for(size_t i=0;i<count;i++)if(back[i]!=uint8_t((checked+i)*37+11)){match=false;break;}
  checked+=count;Watchdog.kick();
 }
 bool closed=close(fd)==0;
 if(timer)detachTimerInterrupt();
 Serial.printf("# IMU_DIAGNOSTIC samples=%lu errors=%lu\n",(unsigned long)imuCount.load(),(unsigned long)imuErrors.load());
 Serial.printf("# PHASE_END file=%s chunk=%u timer=%u bytes=%u writes=%lu error=%d sync=%u match=%u checked=%u close=%u write_us=%llu max_write_us=%llu ticks=%lu\n",path,unsigned(chunk),unsigned(timer),unsigned(done),(unsigned long)writes,error,unsigned(synced),unsigned(match),unsigned(checked),unsigned(closed),(unsigned long long)writeUs,(unsigned long long)maxUs,(unsigned long)ticks);
}
void setup(){
 Serial.begin(115200);LowPower.begin();LowPower.clockMode(CLOCK_MODE_156MHz);Watchdog.begin();Watchdog.start(15000);
 pinMode(PIN_D03,INPUT);Serial.println("# SD_DIAGNOSTIC standalone new_files_only no_gnss no_imu no_subcore");
 if(!card.begin()){Serial.printf("# MOUNT_FAILED errno=%d\n",errno);return;}
 inspect();
 Serial.println("# READY send a=512,timer_off b=6000,timer_off c=512,timer_on d=6000,timer_on");
}
void loop(){
 Watchdog.kick();if(Serial.available()){
  int c=Serial.read();if(c=='a')phase(512,false);if(c=='b')phase(6000,false);if(c=='c')phase(512,true);if(c=='d')phase(6000,true);if(c=='v')inspect();if(c=='e'){startImu();phase(512,false);}if(c=='f'){startImu();phase(6000,false);}if(c=='g'){startImu();phase(512,true);}if(c=='h'){startImu();phase(6000,true);}if(c=='j'){startImu();phase(1024,false);}if(c=='k'){startImu();phase(4096,false);}if(c=='l'){startImu();phase(6000,true,true);}
 }usleep(1000);
}
