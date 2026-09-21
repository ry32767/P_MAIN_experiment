#pragma once
// All filesystem work runs on core 0. Tether ISR/core 1 only moves packets.
static bool localControlBusy=false,localControlDone=false;
static aq::Control localCommand,localAnswer;
static uint32_t localBegan=0;
static void completeControl(uint8_t error=aq::OK){localAnswer.error=error;localControlBusy=false;localControlDone=true;}
static bool beginControl(const aq::Control& c){
 if(localControlBusy || dumping)return false;
 localCommand=c;localAnswer={};localAnswer.op=c.op;localControlDone=false;localControlBusy=true;localBegan=millis();
 if(c.op==aq::RECORD_STOP || c.op==aq::RECORD_NEW){
#ifndef BOARD_PARENT
  monitorStop();monitorWeb=false;
#endif
  stopLog();
 }
 return true;
}
static void executeControl(){
 if(!localControlBusy)return;
 auto c=localCommand;
 if(millis()-localBegan>4000){completeControl(aq::TIMEOUT);return;}
 if(c.op==aq::RECORD_STOP || c.op==aq::RECORD_NEW){
  if(stopRequested)return;
  if(c.op==aq::RECORD_NEW){completeControl(startRecording()?aq::OK:aq::SD_ERROR);return;}
  completeControl(strcmp(sdReason,"STOPPED")==0?aq::OK:aq::SD_ERROR);return;
 }
 if(c.op==aq::RECORD_START){completeControl(!monitorActive && startRecording()?aq::OK:aq::SD_ERROR);return;}
 if(c.op==aq::INFO){
  localAnswer.arg=imuRateHz;String s=String(sdReason);s.toCharArray((char*)localAnswer.data,sizeof(localAnswer.data));localAnswer.length=s.length();completeControl();return;
 }
#ifndef BOARD_PARENT
 if(c.op==aq::IMU_RESET || c.op==aq::RATE_50 || c.op==aq::RATE_100){
  if(!stopped || monitorActive){completeControl(aq::NOT_STOPPED);return;}
  if(c.op!=aq::IMU_RESET)imuRateHz=c.op==aq::RATE_50?50:100;
  resetBno=true;completeControl();return;
 }
 if(c.op==aq::RX_START){monitorWeb=true;monitorStart();completeControl(monitorActive?aq::OK:aq::NOT_STOPPED);return;}
 if(c.op==aq::RX_STOP){monitorStop();monitorWeb=false;completeControl();return;}
 if(c.op==aq::RX_SAMPLE){
  int n=snprintf((char*)localAnswer.data,sizeof(localAnswer.data),"%lu,%lu,%lu,%lu,%lu,%u,%lu",(unsigned long)monitorMin,(unsigned long)(monitorBins?monitorSum/monitorBins:0),(unsigned long)monitorPeak,(unsigned long)edgeCount.load(),(unsigned long)monitorLost,unsigned(digitalRead(PIN_RX_COMP)),(unsigned long)monitorBins);
  localAnswer.length=n;localAnswer.arg=monitorActive;monitorMin=3300;monitorPeak=monitorBins=0;monitorSum=0;completeControl();return;
 }
#endif
 if(c.op==aq::FILE_LIST || c.op==aq::READ_LOG){
  if(!stopped || stopRequested || monitorActive){completeControl(aq::NOT_STOPPED);return;}
  if(c.op==aq::FILE_LIST){
   FsFile dir=sd.open("/",O_RDONLY),f;uint32_t i=0;char name[64];
   if(!dir){completeControl(aq::SD_ERROR);return;}
   while(f.openNext(&dir,O_RDONLY)){
    if(f.isFile() && f.getName(name,sizeof(name)) && (String(name).endsWith(".csv") || String(name).endsWith(".CSV"))){
     if(i++==c.arg){localAnswer.arg=uint32_t(f.fileSize());localAnswer.length=strlen(name);memcpy(localAnswer.data,name,localAnswer.length);f.close();break;}
    }f.close();
   }dir.close();completeControl();return;
  }
  c.data[63]=0;
  if(!aq::safeFilename((const char*)c.data)){completeControl(aq::BAD_ARGUMENT);return;}
  FsFile f=sd.open((const char*)c.data,O_RDONLY);
  if(!f || !f.isFile()){completeControl(aq::NOT_FOUND);return;}
  uint64_t size=f.fileSize();
  if(size>0xffffffff || c.arg>size || !f.seekSet(c.arg)){f.close();completeControl(aq::BAD_ARGUMENT);return;}
  int n=f.read(localAnswer.data,sizeof(localAnswer.data));localAnswer.arg=uint32_t(size);f.close();
  if(n<0){completeControl(aq::SD_ERROR);return;}localAnswer.length=n;completeControl();return;
 }
 completeControl(aq::UNSUPPORTED);
}
static void controlTick(){
#ifndef BOARD_PARENT
 static aq::Packet active,cached;static bool have=false,cacheValid=false;
 aq::Packet p;
 if(!have && controlIn.pop(p)){
  if(cacheValid && p.session==cached.session && p.seq==cached.seq){controlOut.push(cached);}
  else if(beginControl(aq::unpack(p))){active=p;have=true;}
 }
#endif
 executeControl();
#ifndef BOARD_PARENT
 if(have && localControlDone){
  active.kind=aq::CONTROL_REPLY;aq::pack(active,localAnswer);
  if(controlOut.push(active)){cached=active;cacheValid=true;have=false;localControlDone=false;}
 }
#endif
}
