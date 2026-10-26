#pragma once
// Core 0 owns these files. Never changes sensor coefficients or existing logs.
#include "body_calibration.h"
bodycal::Profile bodyProfile;bool bodyLoaded=false,bodyRestoreError=false;
uint32_t bodyDcdGeneration=0;bodycal::Capture bodyCapture;FsFile bodyCaptureFile;
std::atomic<bool> bodyRecordingActive{false};
std::atomic<bool> bodyCameraReadyAtomic{false};bodycal::Motion bodyMotion;
bool bodyMotionAccepted=false;bool bodyEvent(uint8_t event,uint8_t phase);
uint32_t bodyCameraBoot=0,bodyCameraImu=0,bodyCameraMag=0;char bodyCameraLog[64]{};
uint32_t bodyLastAxesCapture=0;
uint32_t bodyLastCapture=0;char bodyCaptureName[64]{};
uint32_t bodyProfileLogGeneration=0;
bool bodyActive(){return bodyCapture.active;}
const char* bodyPrefix(){return role[0]=='P'?"P_MAIN":"C_MAIN";}
void bodyRefs(uint32_t& imu,uint32_t& mag){
#ifdef BOARD_PARENT
 auto s=spresenseSnapshot();imu=spresenseFresh(s,time_us_64())&&s.calibrationApplied&&!s.calibrationSaveError&&!s.calibrationRestoreError?s.calibrationGeneration:0;
 mag=i2c_mag_coefficients().enabled?magRecordSequence:0;
#else
 imu=mag=bodyDcdGeneration;
#endif
}
bool bodyIndividualReady(){uint32_t i,m;bodyRefs(i,m);bool ok=i&&m&&!calibrationActive;
#ifdef BOARD_PARENT
 return ok&&!parentCalibrationRestoreError&&parentCalibrationMatches()&&!i2c_mag_calibration().active;
#else
 auto h=health();return ok&&(h.flags&aq::IMU)&&h.imuAgeUs<500000;
#endif
}
bool bodyClockReady(){
#ifdef BOARD_PARENT
 auto now=time_us_64();auto s=spresenseSnapshot();auto u=utcSnapshot();
 return spresenseFresh(s,now)&&s.recording&&s.imuTimeReady&&s.ppsLocked&&s.synchronized&&s.timeSource==1&&u.source==1&&u.at(now);
#else
 return true;
#endif
}
bool bodyExperimentReady(){uint32_t i,m;bodyRefs(i,m);return bodyLoaded&&!bodyRestoreError&&bodyProfile.capture==bodyLastAxesCapture&&bodyProfile.axes==3&&bodyIndividualReady()&&bodycal::matches(bodyProfile,i,m)&&!bodyActive();}
bool bodyCameraReady(){uint32_t i,m;bodyRefs(i,m);return bodyCameraReadyAtomic&&bodyIndividualReady()&&!bodyActive()&&!stopped&&!stopRequested&&!calibrationActive&&sdHealthy&&bodyCameraBoot==sessionId&&i&&m&&i==bodyCameraImu&&m==bodyCameraMag&&!strcmp(bodyCameraLog,logName);}
void bodyObserveMotion(float x,float y,float z,uint64_t at,uint32_t seq){if(!bodyActive()||bodyCapture.phase!=5||!isfinite(x)||!isfinite(y)||!isfinite(z))return;uint8_t bits=(fabsf(x)>.08f?1:0)|(fabsf(y)>.08f?2:0)|(fabsf(z)>.08f?4:0);bodyMotion.observe(bits,at,seq);}
bool bodyFinish(){
 if(!bodyCapture.complete()||!bodyMotionAccepted)return false;
 bool ok=bodyEvent(4,0);bodyCaptureFile.close();bodyCapture.active=false;bodyCapture.phase=0;bodyRecordingActive=false;
 if(ok){bodyCameraBoot=sessionId;bodyCameraImu=bodyCapture.imu;bodyCameraMag=bodyCapture.mag;strncpy(bodyCameraLog,logName,sizeof(bodyCameraLog)-1);bodyCameraReadyAtomic=true;}return ok;
}
#ifdef BOARD_PARENT
bool bodyRotateMag(const float* raw,float* out){
 uint32_t i,m;bodyRefs(i,m);if(!bodyLoaded||bodyActive()||bodyProfile.capture!=bodyLastAxesCapture||!(bodyProfile.axes&2)||!bodycal::matches(bodyProfile,i,m))return false;
 parentcal::Mount mount;memcpy(mount.q,bodyProfile.magQ,sizeof(mount.q));parentcal::rotate(mount,raw,out);return true;
}
#endif
void loadBodyCalibration(){
 bodyLoaded=false;bodyRestoreError=false;bodyDcdGeneration=0;bodyLastCapture=0;bodyLastAxesCapture=0;bodyProfile={};bodyProfileLogGeneration=0;bodyCameraReadyAtomic=false;bodyMotion={};bodyMotionAccepted=false;
 FsFile dir=sd.open("/",O_RDONLY),f;char name[80];if(!dir){bodyRestoreError=true;return;}
 while(f.openNext(&dir,O_RDONLY)){
  if(f.isFile()&&f.getName(name,sizeof(name))){
   String n=name;String prefix=String(bodyPrefix());
   if(n.endsWith(".bin")&&(n.startsWith(prefix+"_BODY_ALIGN_")||n.startsWith(prefix+"_BNO_CAL_"))){
    bodycal::Profile p;bool ok=f.fileSize()==sizeof(p)&&f.read(&p,sizeof(p))==sizeof(p)&&bodycal::valid(p)&&!strcmp(p.device,rp2040.getChipID());
    if(!ok)bodyRestoreError=true;
    else if(n.startsWith(prefix+"_BNO_CAL_")){if(p.generation>bodyDcdGeneration)bodyDcdGeneration=p.generation;}
    else if(!bodyLoaded||p.generation>bodyProfile.generation){bodyProfile=p;bodyLoaded=true;}
   }
   bool axes=n.startsWith(prefix+"_BODY_CAPTURE_");
   if((axes||n.startsWith(prefix+"_CAMERA_CAPTURE_"))&&n.endsWith(".bin")){
    unsigned v=0;if(sscanf(name+strlen(bodyPrefix())+(axes?14:16),"%5u",&v)==1){if(v>bodyLastCapture)bodyLastCapture=v;if(axes&&v>bodyLastAxesCapture)bodyLastAxesCapture=v;}
   }
  }f.close();
 }dir.close();
}
bool bodyPersist(bodycal::Profile& p,bool receipt=false){
 uint32_t first=receipt?bodyDcdGeneration+1:bodyLoaded?bodyProfile.generation+1:1;
 for(uint32_t n=first;n&&n<100000;n++){
  char path[64],tmp[64];const char* kind=receipt?"BNO_CAL":"BODY_ALIGN";
  snprintf(path,sizeof(path),"%s_%s_%05lu.bin",bodyPrefix(),kind,(unsigned long)n);
  snprintf(tmp,sizeof(tmp),"%s_%s_%05lu.tmp",bodyPrefix(),kind,(unsigned long)n);
  if(sd.exists(path)||sd.exists(tmp))continue;
  p.generation=n;strncpy(p.device,rp2040.getChipID(),sizeof(p.device)-1);auto a=utcSnapshot();p.utc=a.at(time_us_64());p.utcSource=p.utc?a.source:0;bodycal::seal(p);
  FsFile f=sd.open(tmp,O_RDWR|O_CREAT|O_EXCL);if(!f)return false;bodycal::Profile check;
  bool ok=f.write(&p,sizeof(p))==sizeof(p)&&f.sync()&&f.seekSet(0)&&f.read(&check,sizeof(check))==sizeof(check)&&!memcmp(&p,&check,sizeof(p))&&bodycal::valid(check);f.close();
  if(!ok||!sd.rename(tmp,path))return false;
  if(receipt)bodyDcdGeneration=n;else{bodyProfile=p;bodyLoaded=true;}return true;
 }return false;
}
bool bodySaveDcdReceipt(){bodycal::Profile p;return stopped&&!stopRequested&&!dumping&&powered&&sdCardPresent()&&!strcmp(sdReason,"STOPPED")&&bodyPersist(p,true);}
bool bodyEvent(uint8_t event,uint8_t phase){
 bodycal::Event e;e.capture=bodyCapture.id;e.boot=sessionId;e.imu=bodyCapture.imu;e.mag=bodyCapture.mag;e.mono=time_us_64();auto u=utcSnapshot();e.utc=u.at(e.mono);e.utcSource=e.utc?u.source:0;
 auto h=health();e.sequence=h.imuSamples;e.drops=h.dropped+h.imuGaps;e.event=event;e.phase=phase;e.mask=bodyCapture.mask;
#ifdef BOARD_PARENT
 auto sp=spresenseSnapshot();e.sequence=sp.nav.imu_samples;e.drops=sp.nav.imu_gaps;
#endif
 strncpy(e.device,rp2040.getChipID(),sizeof(e.device)-1);strncpy(e.log,logName,sizeof(e.log)-1);strncpy(e.raw,rawName,sizeof(e.raw)-1);strncpy(e.health,slowName,sizeof(e.health)-1);
 if(!writeBinary(bodyCaptureFile,aqbin::BODY_EVENT,e.mono,event,e)||!bodyCaptureFile.sync())return false;return true;
}
void bodyAbort(){if(bodyCapture.active){if(sdHealthy&&bodyCaptureFile)bodyEvent(5,bodyCapture.phase);bodyCaptureFile.close();bodyCapture.active=false;bodyCapture.phase=0;}bodyRecordingActive=false;}
bool bodyBegin(bool cameraOnly=false){
 if(cameraOnly&&!bodyExperimentReady())return false;
 if(bodyActive()||!sdHealthy||stopped||stopRequested||dumping||runRanging||calibrationActive||!bodyIndividualReady())return false;
#ifdef BOARD_PARENT
 if(!bodyClockReady())return false;
#else
 if(!(health().flags&aq::IMU))return false;
#endif
 uint32_t i,m;bodyRefs(i,m);
 for(uint32_t n=bodyLastCapture+1;n&&n<100000;n++){
  snprintf(bodyCaptureName,sizeof(bodyCaptureName),"%s_%s_CAPTURE_%05lu.bin",bodyPrefix(),cameraOnly?"CAMERA":"BODY",(unsigned long)n);if(sd.exists(bodyCaptureName))continue;
  bodyCaptureFile=sd.open(bodyCaptureName,O_WRONLY|O_CREAT|O_EXCL);if(!bodyCaptureFile)return false;
  aqbin::FileHeader h;h.role=role[0]=='P'?1:2;h.stream=aqbin::BODY_EVENT;h.session=sessionId;h.createdUs=time_us_64();aqbin::seal(h);
  if(bodyCaptureFile.write(&h,sizeof(h))!=sizeof(h)||!bodyCaptureFile.sync()){bodyCaptureFile.close();return false;}
  bodyCapture.begin(n,i,m,time_us_64(),cameraOnly?16:31);bodyLastCapture=n;if(!cameraOnly)bodyLastAxesCapture=n;bodyRecordingActive=true;bodyCameraReadyAtomic=false;bodyMotion={};bodyMotionAccepted=false;
  if(!bodyEvent(cameraOnly?6:1,0)){bodyAbort();return false;}return true;
 }return false;
}
bool bodyCompleted(const bodycal::Solution& s){
 char name[64];snprintf(name,sizeof(name),"%s_BODY_CAPTURE_%05lu.bin",bodyPrefix(),(unsigned long)s.capture);
 FsFile f=sd.open(name,O_RDONLY);if(!f)return false;
 aqbin::RecordHeader h;bodycal::Event e;aqbin::FileHeader head;bodycal::Capture check;
 bool ok=f.read(&head,sizeof(head))==sizeof(head)&&aqbin::valid(head)&&head.role==(role[0]=='P'?1:2)&&head.session==s.boot&&f.fileSize()<=32768;
 bool finished=false;unsigned count=0;
 while(ok&&f.position()<f.fileSize()){
  ok=++count<=32&&!finished&&f.read(&h,sizeof(h))==sizeof(h)&&h.kind==aqbin::BODY_EVENT&&h.bytes==sizeof(e)&&f.read(&e,sizeof(e))==sizeof(e)&&aqbin::valid(h,&e)&&h.monoUs==e.mono&&e.capture==s.capture&&e.boot==s.boot&&e.imu==s.imu&&e.mag==s.mag&&!strcmp(e.device,rp2040.getChipID());
  if(!ok)break;
  if(e.event==1)ok=count==1&&check.begin(e.capture,e.imu,e.mag,e.mono);
  else if(e.event==2||e.event==3)ok=check.mark(e.phase,e.event==3,e.mono)&&e.mask==check.mask;
  else if(e.event==4){ok=check.complete()&&e.mask==31;finished=ok;}
  else ok=false;
 }f.close();return ok&&finished;
}
bool bodyImport(const bodycal::Solution& s){
 if(bodyActive()||!stopped||stopRequested||runRanging||calibrationActive||!sdCardPresent()||dumping||s.reserved[0]||s.reserved[1]||!bodycal::values(s.axes,s.camera,s.imuQ,s.magQ,s.cameraScale,s.cameraOffsetUs)||!bodyCompleted(s))return false;
 uint32_t i,m;bodyRefs(i,m);if(!i||!m||i!=s.imu||m!=s.mag)return false;
 bodycal::Profile p;p.capture=s.capture;p.boot=s.boot;p.imu=i;p.mag=m;p.axes=s.axes;p.camera=s.camera;
 memcpy(p.imuQ,s.imuQ,sizeof(p.imuQ));memcpy(p.magQ,s.magQ,sizeof(p.magQ));p.cameraScale=s.cameraScale;p.cameraOffsetUs=s.cameraOffsetUs;return bodyPersist(p);
}
String bodyJson(){uint64_t now=time_us_64();return "{\"a\":"+String(bodyActive()?1:0)+",\"m\":"+String(bodyCapture.mask)+",\"p\":"+String(bodyCapture.phase)+",\"c\":"+String(bodyLastCapture)+",\"g\":"+String(bodyLoaded?bodyProfile.generation:0)+",\"v\":"+String(bodyLoaded?bodyProfile.axes:0)+",\"t\":"+String(bodyLoaded?bodyProfile.camera:0)+",\"r\":"+String(bodyExperimentReady()?1:0)+",\"b\":"+String(bodyDcdGeneration)+",\"k\":"+String(bodyCameraReady()?1:0)+",\"o\":"+String(bodyCapture.required)+",\"d\":"+String(bodyMotion.axes)+",\"s\":"+String(bodyMotion.began?unsigned(((now-bodyMotion.began)/1000000)>900?900:((now-bodyMotion.began)/1000000)):0)+",\"q\":"+String(bodyMotion.ready(now)?2:bodyMotion.have&&now-bodyMotion.updated<5000000?1:0)+"}";}
void bodyTick(){uint32_t i,m;bodyRefs(i,m);if(bodyActive()&&(!sdHealthy||stopped||stopRequested||!bodyClockReady()||time_us_64()-bodyCapture.started>bodycal::maximumSessionUs||i!=bodyCapture.imu||m!=bodyCapture.mag))bodyAbort();
 if(!bodyCameraReady())bodyCameraReadyAtomic=false;
#ifdef BOARD_PARENT
 if(bodyActive()&&bodyCapture.phase==5){auto s=spresenseSnapshot();if(s.motionAvailable&&spresenseFresh(s,time_us_64()))bodyMotion.observe(s.motionAxes,s.receivedUs,s.nav.imu_samples);}
#endif
 if(sdHealthy&&!stopped&&!stopRequested&&bodyExperimentReady()&&bodyProfileLogGeneration!=bodyProfile.generation&&writeBinary(slowFile,aqbin::BODY_PROFILE,time_us_64(),bodyProfile.generation,bodyProfile))bodyProfileLogGeneration=bodyProfile.generation;
}
