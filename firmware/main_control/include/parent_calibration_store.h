#pragma once
#include "parent_calibration.h"
parentcal::Record parentCalibration;
bool parentCalibrationLoaded=false,parentCalibrationRestoreError=false;
String parentCalibrationError;
extern uint32_t magRecordSequence;
void loadParentCalibration(){
 FsFile directory=sd.open("/",O_RDONLY),file;parentcal::Record best;bool found=false;
 parentCalibration=parentcal::Record{};parentCalibrationLoaded=false;parentCalibrationRestoreError=false;parentSetGeneration=0;
 if(!directory){parentCalibrationRestoreError=true;return;}
 char name[80];
 while(file.openNext(&directory,O_RDONLY)){
  if(file.isFile() && file.getName(name,sizeof(name)) && String(name).startsWith("P_MAIN_CALSET_") && String(name).endsWith(".bin")){
   parentcal::Record candidate;
   bool ok=file.fileSize()==sizeof(candidate) && file.read(&candidate,sizeof(candidate))==sizeof(candidate) && parentcal::valid(candidate);
   if(!ok)parentCalibrationRestoreError=true;
   else if(!found || candidate.generation>best.generation){best=candidate;found=true;}
  }file.close();
 }directory.close();
 if(found){parentCalibration=best;parentCalibrationLoaded=true;parentSetGeneration=best.generation;}
}
bool saveParentCalibration(parentcal::Record candidate){
 // New immutable record, never modify/delete any previous profile or log.
 uint32_t first=parentCalibrationLoaded?parentCalibration.generation+1:1;
 if(!first)return false;
 for(uint32_t n=first;n<100000;n++){
  char name[64],temporary[64];snprintf(name,sizeof(name),"P_MAIN_CALSET_%05lu.bin",(unsigned long)n);
  snprintf(temporary,sizeof(temporary),"P_MAIN_CALSET_%05lu.tmp",(unsigned long)n);if(sd.exists(name)||sd.exists(temporary))continue;
  candidate.generation=n;candidate.parentSession=sessionId;auto a=utcSnapshot();uint64_t now=time_us_64();
  candidate.createdUtc=a.at(now);candidate.utcSource=candidate.createdUtc?a.source:0;parentcal::seal(candidate);
  if(!parentcal::valid(candidate))return false;
  FsFile file=sd.open(temporary,O_RDWR|O_CREAT|O_EXCL);if(!file)return false;parentcal::Record readback;
  bool ok=file.write(&candidate,sizeof(candidate))==sizeof(candidate) && file.sync() && file.seekSet(0) &&
   file.read(&readback,sizeof(readback))==sizeof(readback) && !memcmp(&candidate,&readback,sizeof(candidate)) && parentcal::valid(readback);
  file.close();if(!ok || !sd.rename(temporary,name))return false;
  parentCalibration=candidate;parentCalibrationLoaded=true;parentSetGeneration=n;return true;
 }return false;
}
bool parentCalibrationMatches(){
 auto s=spresenseSnapshot();return parentCalibrationLoaded && spresenseFresh(s,time_us_64()) && s.calibrationApplied &&
  i2c_mag_coefficients().enabled && parentCalibration.imuCalibration && parentCalibration.magCalibration &&
  parentCalibration.imuCalibration==s.calibrationGeneration && parentCalibration.magCalibration==magRecordSequence;
}
String parentCalibrationJson(){
 const auto& c=parentCalibration;auto sp=spresenseSnapshot();bool fresh=spresenseFresh(sp,time_us_64());
 String out="{\"loaded\":"+String(parentCalibrationLoaded?"true":"false")+",\"generation\":"+String(parentSetGeneration.load())+
  ",\"geometry_complete\":"+String(parentCalibrationLoaded&&parentcal::geometryComplete(c)?"true":"false")+
  ",\"calibrations_match\":"+String(parentCalibrationMatches()?"true":"false")+
  ",\"restore_error\":"+String(parentCalibrationRestoreError?"true":"false")+",\"accuracy_verified\":false,\"frame\":\"FRD\",\"error\":"+ntrip::jsonString(parentCalibrationError);
 out+=",\"name\":"+ntrip::jsonString(c.name)+",\"cad\":"+ntrip::jsonString(c.cad)+",\"body\":"+ntrip::jsonString(c.body)+
  ",\"imu_calibration\":"+String(c.imuCalibration)+",\"mag_calibration\":"+String(c.magCalibration)+",\"current_mag_calibration\":"+String(magRecordSequence)+
  ",\"current_imu_calibration\":"+String(fresh?sp.calibrationGeneration:0)+",\"can_save\":"+String(stopped&&!stopRequested&&!runRanging&&!dumping&&(!fresh||(!sp.recording&&!sp.commandPending&&!(sp.calibration>=1&&sp.calibration<=4)))&&!i2c_mag_calibration().active?"true":"false")+",\"sensors\":[";
 for(unsigned i=0;i<3;i++){
  if(i)out+=',';const auto& m=c.mounts[i];out+="{\"id\":"+ntrip::jsonString(c.sensor[i])+",\"known\":"+String(m.known?"true":"false")+",\"position\":[";
  for(unsigned j=0;j<3;j++){if(j)out+=',';out+=String(m.position[j],6);}out+="],\"q\":[";
  for(unsigned j=0;j<4;j++){if(j)out+=',';out+=String(m.q[j],7);}out+="]}";
 }return out+"]}";
}
