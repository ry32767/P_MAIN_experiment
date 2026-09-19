#include <Arduino.h>
#include <SerialPIO.h>
#include <SPI.h>
#include <SdFat.h>
#include <Wire.h>
#include <AquaBeacon.h>
#include <pico/mutex.h>
#include <pico/time.h>
#include <pico/rand.h>
#include <hardware/sync.h>
#include <atomic>
#include "pins.h"
#if AQ_WIFI
#include <WiFi.h>
#include "web_page.h"
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#endif

struct Snapshot {
  aq::Nav nav;
  uint64_t sampled_us=0, utc_us=0, received_us=0, pps_us=0;
  uint32_t packets=0, crc_errors=0, rejected=0, pps_count=0, period_us=0, uart_age_us=0, rx_overflows=0;
  bool synchronized=false;
};
mutex_t snapshotMutex;
bool core1_separate_stack = true;
Snapshot shared;
std::atomic<bool> sharedReady{false};
volatile uint64_t irqEdge=0;
volatile uint32_t irqCount=0;
SerialPIO timeLink(-1,pins::spresense_rx,256); // PIO RX only; GP7 is NOT UART1 RX.
void onPps() { irqEdge=time_us_64(); ++irqCount; }
void setup1() {
  while(!sharedReady.load(std::memory_order_acquire)) tight_loop_contents();
  pinMode(pins::pps,INPUT); pinMode(pins::spresense_rx,INPUT);
  attachInterrupt(digitalPinToInterrupt(pins::pps),onPps,RISING);
  timeLink.begin(115200);
}
void loop1() {
  static aq::Parser parser;
  static aq::Clock clock;
  static Snapshot current;
  static uint32_t seen=0;
  uint32_t saved=save_and_disable_interrupts();
  uint64_t edge=irqEdge; uint32_t count=irqCount;
  restore_interrupts(saved);
  if(count!=seen) {
    clock.pps(edge); seen=count; current.pps_us=edge; current.pps_count=count;
  }
  aq::Nav nav;
  if(timeLink.overflow()) { ++current.rx_overflows; parser.reset(); clock.invalidate(); }
  while(timeLink.available()) {
    if(parser.push(uint8_t(timeLink.read()),nav)) {
      // This core never performs SD, I2C or Wi-Fi operations.
      current.received_us=time_us_64(); current.nav=nav; ++current.packets;
      clock.accept(nav,current.received_us);
    }
  }
  current.sampled_us=time_us_64(); current.synchronized=clock.locked(current.sampled_us);
  current.utc_us=clock.utc(current.sampled_us); current.crc_errors=parser.errors;
  current.rejected=clock.rejected; current.period_us=clock.interval_us; current.uart_age_us=clock.uart_age_us;
  mutex_enter_blocking(&snapshotMutex); shared=current; mutex_exit(&snapshotMutex);
  delayMicroseconds(100);
}
Snapshot snapshot() {
  mutex_enter_blocking(&snapshotMutex); Snapshot s=shared; mutex_exit(&snapshotMutex); return s;
}

SdFs SD;
FsFile logFile;
char logName[16]="";
bool powerReady=false, sdMounted=false, sdVerified=false, logging=false, inaOk=false;
uint32_t sdErrors=0, rows=0, missedRows=0, maxWriteUs=0, lastFlush=0;
float busV=0, currentA=0;
const char *sdError="NOT_STARTED";
const char *csvHeader="row,mono_us,utc_us,sync,pps_count,pps_period_us,uart_age_us,nav_seq,nav_utc_s,nav_flags,fix,satellites,lat_e7,lon_e7,altitude_mm,rx_packets,crc_errors,sync_rejected,ina_ok,bus_v,current_a,ina_alert,sp_imu_samples,sp_imu_errors,sp_imu_gaps,sp_sd_errors,sp_sd_rows,sd_errors,missed_rows,heap_free,rx_overflows,crc32\n";
void sdFailure(const char *reason) {
  sdError=reason; ++sdErrors; logging=false; sdVerified=false;
  if(logFile) logFile.close();
  Serial.printf("# SD_ERROR %s\n",reason);
}
bool stopLog() {
  if(logFile) {
    bool ok=logFile.sync(); ok=logFile.close() && ok; logging=false;
    if(!ok) { sdFailure("FLUSH_FAILED"); return false; }
  }
  logging=false; Serial.println("# STOPPED_REMOVE_SD"); return true;
}
bool startLog() {
  if(logging) return true;
  if(!sdMounted || !sdVerified) return false;
  for(unsigned id=0;id<100000;id++) {
    snprintf(logName,sizeof(logName),"P%05u.CSV",id);
    if(!SD.exists(logName)) {
      logFile=SD.open(logName,O_WRONLY|O_CREAT|O_EXCL);
      if(!logFile || logFile.print(csvHeader)!=strlen(csvHeader)) { sdFailure("CREATE_FAILED"); return false; }
      if(!logFile.sync()) { sdFailure("HEADER_FLUSH_FAILED"); return false; }
      logging=true; rows=0; lastFlush=millis(); sdError="NONE";
      Serial.printf("# LOG %s\n",logName); return true;
    }
  }
  sdFailure("NAMES_EXHAUSTED"); return false;
}
bool mountSd() {
  if(!powerReady) return false;
  stopLog(); SD.end(); sdMounted=false; sdVerified=false;
  SPI1.setSCK(pins::sd_sck); SPI1.setTX(pins::sd_mosi); SPI1.setRX(pins::sd_miso);
  if(!SD.begin(SdSpiConfig(pins::sd_cs,SHARED_SPI,SD_SCK_MHZ(1),&SPI1))) { sdFailure("MOUNT_FAILED"); return false; }
  sdMounted=true;
  char name[16]; unsigned id=0;
  for(;id<100000;id++) { snprintf(name,sizeof(name),"V%05u.BIN",id); if(!SD.exists(name)) break; }
  if(id==100000) { sdFailure("VERIFY_NAMES_EXHAUSTED"); return false; }
  uint8_t pattern[512], back[512];
  for(size_t i=0;i<sizeof(pattern);i++) pattern[i]=uint8_t(i*37+id);
  FsFile test=SD.open(name,O_WRONLY|O_CREAT|O_EXCL);
  bool ok=test && test.write(pattern,sizeof(pattern))==sizeof(pattern);
  ok=test.sync() && ok; ok=test.close() && ok;
  test=SD.open(name,O_RDONLY);
  ok=test && test.read(back,sizeof(back))==sizeof(back) && !memcmp(pattern,back,sizeof(back)) && ok;
  test.close(); SD.remove(name); // Newly-created verification file only.
  if(!ok) { sdFailure("READBACK_FAILED"); return false; }
  sdVerified=true; Serial.println("# SD_READBACK_OK"); return startLog();
}
bool readRegister(uint8_t reg,uint16_t &value) {
  Wire.beginTransmission(0x40); Wire.write(reg);
  if(Wire.endTransmission(false)!=0 || Wire.requestFrom(0x40,2)!=2) return false;
  value=(uint16_t(Wire.read())<<8)|Wire.read(); return true;
}
void samplePower() {
  uint16_t id, bus, shunt;
  inaOk=readRegister(0xfe,id) && id==0x5449 && readRegister(2,bus) && readRegister(1,shunt);
  if(inaOk) { busV=bus*0.00125f; currentA=int16_t(shunt)*0.0000025f/0.020f; }
}
void startPeripherals() {
  // J_PWR1 pin5/6 power the peripheral 3.3V rail. USB alone does not.
  if(analogRead(pins::buttons)<3474) return;
  powerReady=true;
  Wire.setSDA(pins::sda); Wire.setSCL(pins::scl); Wire.begin(); Wire.setClock(100000);
  Wire.setTimeout(10);
  Wire.beginTransmission(0x40); Wire.write(0); Wire.write(0x45); Wire.write(0x27); Wire.endTransmission();
  samplePower(); mountSd();
}
void writeRow(const Snapshot &s) {
  if(!logging) return;
  if(digitalRead(pins::sd_cd)!=LOW) { sdFailure("CARD_REMOVED"); return; }
  char line[640];
  int len=snprintf(line,sizeof(line),"%lu,%llu,%llu,%u,%lu,%lu,%lu,%lu,%llu,%u,%u,%u,%ld,%ld,%ld,%lu,%lu,%lu,%u,%.5f,%.6f,%u,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu",
    (unsigned long)rows,(unsigned long long)s.sampled_us,(unsigned long long)s.utc_us,unsigned(s.synchronized),
    (unsigned long)s.pps_count,(unsigned long)s.period_us,(unsigned long)s.uart_age_us,(unsigned long)s.nav.sequence,(unsigned long long)s.nav.utc_s,
    s.nav.flags,s.nav.fix,s.nav.satellites,(long)s.nav.lat_e7,(long)s.nav.lon_e7,(long)s.nav.altitude_mm,
    (unsigned long)s.packets,(unsigned long)s.crc_errors,(unsigned long)s.rejected,unsigned(inaOk),busV,currentA,unsigned(digitalRead(pins::ina_alert)),
    (unsigned long)s.nav.imu_samples,(unsigned long)s.nav.imu_errors,(unsigned long)s.nav.imu_gaps,(unsigned long)s.nav.sd_errors,
    (unsigned long)s.nav.sd_rows,(unsigned long)sdErrors,(unsigned long)missedRows,(unsigned long)rp2040.getFreeHeap(),(unsigned long)s.rx_overflows);
  if(len<0 || size_t(len)+12>=sizeof(line)) { sdFailure("ROW_OVERFLOW"); return; }
  uint32_t crc=aq::crc32((const uint8_t *)line,len);
  snprintf(line+len,sizeof(line)-len,",%08lx\n",(unsigned long)crc);
  uint64_t before=time_us_64();
  if(logFile.print(line)!=strlen(line) || logFile.getWriteError()) { sdFailure("SHORT_WRITE"); return; }
  ++rows;
  if(millis()-lastFlush>=1000) {
    lastFlush=millis();
    if(!logFile.sync()) sdFailure("FLUSH_FAILED");
  }
  uint32_t elapsed=time_us_64()-before; if(elapsed>maxWriteUs) maxWriteUs=elapsed;
}

#if AQ_WIFI
WiFiServer server(80);
WiFiClient client;
FsFile download;
char apPassword[64], request[2048], response[12288];
size_t requestUsed=0, responseSize=0, responseSent=0;
uint32_t clientStarted=0, clientProgress=0;
uint64_t fileRemaining=0;
bool apReady=false, responding=false;
void closeClient() {
  if(download) download.close(); client.stop(1); requestUsed=responseSize=responseSent=0;
  fileRemaining=0; responding=false;
}
void reply(int code,const char *type,const char *body) {
  responseSize=snprintf(response,sizeof(response),"HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n%s",
    code,code==200?"OK":"Error",type,unsigned(strlen(body)),body);
  if(responseSize>=sizeof(response)) { responseSize=0; closeClient(); return; }
  responseSent=0; responding=true;
}
void statusJson(char *out,size_t n) {
  Snapshot s=snapshot(); uint64_t now=time_us_64();
  bool fresh=s.received_us && now-s.received_us<2000000;
  char volts[24]="null", amps[24]="null";
  if(inaOk) { snprintf(volts,sizeof(volts),"%.3f",busV); snprintf(amps,sizeof(amps),"%.4f",currentA); }
  snprintf(out,n,"{\"sync\":%s,\"link_fresh\":%s,\"utc_us\":%llu,\"nav_utc_s\":%llu,\"fix\":%u,\"satellites\":%u,\"lat\":%.7f,\"lon\":%.7f,\"position_valid\":%s,\"pps_count\":%lu,\"pps_period_us\":%lu,\"uart_age_us\":%lu,\"crc_errors\":%lu,\"sync_rejected\":%lu,\"power_ready\":%s,\"bus_v\":%s,\"current_a\":%s,\"sd_verified\":%s,\"logging\":%s,\"file\":\"%s\",\"rows\":%lu,\"sd_errors\":%lu,\"sd_error\":\"%s\",\"missed_rows\":%lu,\"max_write_us\":%lu,\"heap_free\":%lu,\"sp_sd_ok\":%s,\"sp_imu_ok\":%s,\"sp_imu_samples\":%lu,\"sp_imu_errors\":%lu,\"sp_imu_gaps\":%lu,\"sp_sd_errors\":%lu}",
    s.synchronized?"true":"false",fresh?"true":"false",(unsigned long long)s.utc_us,(unsigned long long)s.nav.utc_s,s.nav.fix,s.nav.satellites,
    s.nav.lat_e7/1e7,s.nav.lon_e7/1e7,(fresh && (s.nav.flags&aq::position_valid))?"true":"false",
    (unsigned long)s.pps_count,(unsigned long)s.period_us,(unsigned long)s.uart_age_us,(unsigned long)s.crc_errors,(unsigned long)s.rejected,
    powerReady?"true":"false",volts,amps,sdVerified?"true":"false",logging?"true":"false",logName,(unsigned long)rows,(unsigned long)sdErrors,sdError,
    (unsigned long)missedRows,(unsigned long)maxWriteUs,(unsigned long)rp2040.getFreeHeap(),
    (fresh && (s.nav.flags&aq::sd_ok))?"true":"false",(fresh && (s.nav.flags&aq::imu_ok))?"true":"false",
    (unsigned long)s.nav.imu_samples,(unsigned long)s.nav.imu_errors,(unsigned long)s.nav.imu_gaps,(unsigned long)s.nav.sd_errors);
}
bool validLogName(const char *s) {
  if(strlen(s)!=10 || s[0]!='P' || strcmp(s+6,".CSV")) return false;
  for(int i=1;i<6;i++) if(s[i]<'0'||s[i]>'9') return false;
  return true;
}
void routeRequest() {
  char method[8], path[128];
  if(sscanf(request,"%7s %127s",method,path)!=2) { reply(400,"text/plain","Bad request"); return; }
  char body[3072];
  if(!strcmp(method,"GET") && !strcmp(path,"/")) { reply(200,"text/html; charset=utf-8",WEB_PAGE); return; }
  if(!strcmp(method,"GET") && !strcmp(path,"/api/status")) { statusJson(body,sizeof(body)); reply(200,"application/json",body); return; }
  if(!strcmp(method,"GET") && !strncmp(path,"/api/logs",9) && (path[9]==0 || path[9]=='?')) {
    unsigned page=0;
    if(path[9] && sscanf(path+9,"?page=%u",&page)!=1) { reply(400,"text/plain","Invalid page"); return; }
    if(page>3124) { reply(400,"text/plain","Invalid page"); return; }
    if(!sdMounted) { reply(503,"text/plain","SD unavailable"); return; }
    size_t used=snprintf(body,sizeof(body),"{\"page\":%u,\"files\":[",page); bool first=true;
    // Scan a bounded 32-name window, rather than blocking on a huge directory.
    for(unsigned id=page*32;id<(page+1)*32;id++) {
      char name[16]; snprintf(name,sizeof(name),"P%05u.CSV",id);
      if(SD.exists(name)) {
        FsFile f=SD.open(name,O_RDONLY); if(!f) continue;
        used+=snprintf(body+used,sizeof(body)-used,"%s{\"name\":\"%s\",\"bytes\":%lu,\"active\":%s}",first?"":",",name,(unsigned long)f.size(),(logging&&!strcmp(name,logName))?"true":"false");
        first=false; f.close();
      }
    }
    snprintf(body+used,sizeof(body)-used,"]}"); reply(200,"application/json",body); return;
  }
  if(!strcmp(method,"GET") && !strncmp(path,"/download?name=",15)) {
    const char *name=path+15;
    if(!validLogName(name)) { reply(400,"text/plain","Invalid file name"); return; }
    if(logging && !strcmp(name,logName)) { reply(409,"text/plain","Stop logging before downloading the active file"); return; }
    if(!sdMounted || !(download=SD.open(name,O_RDONLY))) { reply(404,"text/plain","File not found"); return; }
    fileRemaining=download.size();
    responseSize=snprintf(response,sizeof(response),"HTTP/1.1 200 OK\r\nContent-Type: text/csv\r\nContent-Disposition: attachment; filename=\"%s\"\r\nContent-Length: %llu\r\nConnection: close\r\n\r\n",name,(unsigned long long)fileRemaining);
    responseSent=0; responding=true; return;
  }
  if(!strcmp(method,"POST") && (!strcmp(path,"/api/stop") || !strcmp(path,"/api/start"))) {
    // Custom header prevents cross-origin HTML forms from controlling logging.
    if(!strstr(request,"X-AquaBeacon: local\r\n") && !strstr(request,"x-aquabeacon: local\r\n")) { reply(403,"text/plain","Local UI required"); return; }
    bool ok= !strcmp(path,"/api/stop") ? stopLog() : startLog();
    reply(ok?200:503,"application/json",ok?"{\"ok\":true}":"{\"ok\":false}"); return;
  }
  reply(404,"text/plain","Not found");
}
void webTick() {
  if(!apReady) return;
  if(!client) {
    closeClient(); // Also release an interrupted download before accepting a peer.
    client=server.accept();
    if(!client) return;
    clientStarted=clientProgress=millis(); requestUsed=0; responding=false;
  }
  if(!client.connected() || millis()-clientProgress>5000 || (!responding && millis()-clientStarted>2000)) { closeClient(); return; }
  if(!responding) {
    unsigned budget=256;
    while(budget-- && client.available()) {
      if(requestUsed+1>=sizeof(request)) { reply(431,"text/plain","Header too large"); break; }
      request[requestUsed++]=char(client.read()); request[requestUsed]=0; clientProgress=millis();
      if(strstr(request,"\r\n\r\n")) { routeRequest(); break; }
    }
    return;
  }
  int capacity=client.availableForWrite();
  if(capacity<=0) return;
  if(responseSent==responseSize && fileRemaining && download) {
    size_t wanted=fileRemaining<512 ? size_t(fileRemaining):512;
    int got=download.read((uint8_t*)response,wanted);
    if(got<=0) { closeClient(); return; }
    fileRemaining-=got; responseSize=got; responseSent=0;
  }
  size_t count=responseSize-responseSent;
  if(count>512) count=512; if(count>size_t(capacity)) count=capacity;
  if(count) {
    size_t sent=client.write((const uint8_t*)response+responseSent,count);
    responseSent+=sent; if(sent) clientProgress=millis();
  }
  if(responseSent==responseSize && !fileRemaining && client.flush(1)) closeClient();
}
void startWeb() {
#ifdef AQ_AP_PASSWORD
  snprintf(apPassword,sizeof(apPassword),"%s",AQ_AP_PASSWORD);
#else
  snprintf(apPassword,sizeof(apPassword),"AQ%08lx",(unsigned long)get_rand_32());
#endif
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192,168,4,1),IPAddress(192,168,4,1),IPAddress(255,255,255,0));
  apReady=WiFi.softAP("AquaBeacon-PMAIN",apPassword);
  if(apReady) server.begin();
  Serial.printf("# WIFI %s password=%s url=http://192.168.4.1/\n",apReady?"READY":"FAILED",apPassword);
}
#else
void webTick() {}
#endif

void setup() {
  Serial.begin(115200); analogReadResolution(12);
  // Keep acoustic transmitter off for this acquisition experiment.
  digitalWrite(pins::tx_pwm,LOW); pinMode(pins::tx_pwm,OUTPUT);
  pinMode(pins::led,OUTPUT); pinMode(pins::ina_alert,INPUT); pinMode(pins::sd_cd,INPUT);
  mutex_init(&snapshotMutex); sharedReady.store(true,std::memory_order_release);
  startPeripherals();
#if AQ_WIFI
  startWeb();
#endif
  Serial.println("# q=stop r=start s=SD verify/remount h=status");
}
void loop() {
  static uint32_t lastPower=0,lastRow=0,lastStatus=0;
  uint32_t now=millis();
  if(now-lastPower>=1000) {
    lastPower=now;
    if(powerReady) samplePower(); else startPeripherals();
  }
  if(now-lastRow>=100) {
    if(logging && lastRow && now-lastRow>=200) missedRows+=(now-lastRow)/100-1;
    lastRow=now; Snapshot s=snapshot(); writeRow(s);
    digitalWrite(pins::led,s.synchronized ? HIGH : ((now/500)&1));
  }
  if(Serial.available()) {
    int c=Serial.read();
    if(c=='q') stopLog(); if(c=='r') startLog(); if(c=='s') mountSd();
    if(c=='h') {
      Snapshot s=snapshot();
      Serial.printf("# DIAG power_started=%u buttons_adc=%u sd_cd=%u sd_code=0x%02x sd_data=0x%02x ina_ok=%u bus_v=%.5f current_a=%.6f alert=%u\n",powerReady,analogRead(pins::buttons),digitalRead(pins::sd_cd),SD.sdErrorCode(),SD.sdErrorData(),inaOk,busV,currentA,digitalRead(pins::ina_alert));
      Serial.printf("# sync=%u packets=%lu PPS=%lu period_us=%lu uart_age_us=%lu heap=%lu sd=%s file=%s rows=%lu\n",s.synchronized,(unsigned long)s.packets,(unsigned long)s.pps_count,(unsigned long)s.period_us,(unsigned long)s.uart_age_us,(unsigned long)rp2040.getFreeHeap(),sdError,logName,(unsigned long)rows);
#if AQ_WIFI
      Serial.printf("# WIFI password=%s url=http://192.168.4.1/\n",apPassword);
#endif
    }
  }
  if(now-lastStatus>=1000) {
    lastStatus=now; Snapshot s=snapshot();
    Serial.printf("# sync=%u packets=%lu pps=%lu sd_rows=%lu sd_errors=%lu imu=%lu heap=%lu\n",s.synchronized,(unsigned long)s.packets,(unsigned long)s.pps_count,(unsigned long)rows,(unsigned long)sdErrors,(unsigned long)s.nav.imu_samples,(unsigned long)rp2040.getFreeHeap());
  }
  webTick();
}
