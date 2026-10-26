#pragma once
uint32_t magRecordSequence=0;char magRecordName[64]{};bool magLoaded=false;
String magError="";
void loadMagCalibration(){
 FsFile dir=sd.open("/",O_RDONLY),file;magcal::Record r;magcal::Coefficients best;uint32_t sequence=0;bool found=false;char name[64],bestName[64]{};
 if(!dir)return;
 while(file.openNext(&dir,O_RDONLY)){
  if(file.isFile()&&file.getName(name,sizeof(name))&&String(name).startsWith("P_MAIN_MAGCAL_")&&String(name).endsWith(".bin")&&file.fileSize()==sizeof(r)&&file.read(&r,sizeof(r))==sizeof(r)){
   magcal::Coefficients c;if(magcal::decode(r,c)&&(!found||r.sequence>sequence)){found=true;sequence=r.sequence;best=c;strcpy(bestName,name);}
  }file.close();
 }dir.close();
 if(found){i2c_mag_apply(best);magRecordSequence=sequence;strcpy(magRecordName,bestName);magLoaded=true;}
}
bool saveMagCalibration(const magcal::Coefficients& c){
 if(!stopped||stopRequested||dumping||localControlBusy){magError="親機の記録停止完了を待ってください";return false;}
 if(!sdCardPresent()){magError="校正保存には親機SDカードが必要です";return false;}
 for(uint32_t n=magRecordSequence+1;n<100000;n++){
  char name[64];snprintf(name,sizeof(name),"P_MAIN_MAGCAL_%05lu.bin",(unsigned long)n);if(sd.exists(name))continue;
  FsFile f=sd.open(name,O_RDWR|O_CREAT|O_EXCL);if(!f){magError="校正ファイルを作成できません";return false;}
  auto record=magcal::record(c,n);magcal::Record got;
  bool ok=f.write(&record,sizeof(record))==sizeof(record)&&f.sync()&&f.seekSet(0)&&f.read(&got,sizeof(got))==sizeof(got)&&!memcmp(&got,&record,sizeof(got));f.close();
  if(!ok){magError="校正の保存・読戻しに失敗しました";return false;}
  i2c_mag_apply(c);magRecordSequence=n;strcpy(magRecordName,name);magLoaded=true;magError="";return true;
 }magError="校正ファイルの番号上限です";return false;
}
String magJson(){
 auto& c=i2c_mag_calibration();auto r=c.evaluate(millis());const auto& sensor=i2c_readings();const auto& coeff=i2c_mag_coefficients();bool fresh=sensor.lis_raw_ok&&uint32_t(millis()-sensor.lis_sample_ms)<300;
 String s="{\"present\":"+String(i2c_mag_present()?"true":"false")+",\"fresh\":"+String(fresh?"true":"false")+",\"active\":"+String(c.active?"true":"false")+",\"ready\":"+String(r.ready&&fresh?"true":"false")+",\"saved\":"+String(c.saved?"true":"false")+",\"loaded\":"+String(magLoaded?"true":"false")+",\"enabled\":"+String(coeff.enabled?"true":"false")+",\"saturated\":"+String(fresh&&sensor.lis_saturated?"true":"false")+",\"samples\":"+String(c.samples)+",\"rejected\":"+String(c.rejected)+",\"saturated_samples\":"+String(c.saturated)+",\"elapsed_s\":"+String(c.active?uint32_t(millis()-c.startedMs)/1000:0)+",\"radius_cv\":"+String(r.rms,4)+",\"absolute_accuracy_verified\":false,\"error\":"+ntrip::jsonString(magError)+",\"file\":"+ntrip::jsonString(magRecordName);
 s+=",\"fit_evaluated\":"+String(r.fitEvaluated?"true":"false")+",\"radius_match\":"+String(r.radiusMatch?"true":"false");
 auto array=[&](const char* key,const float* v,bool valid=true){s+=",\""+String(key)+"\":[";for(unsigned i=0;i<3;i++){if(i)s+=',';s+=valid&&isfinite(v[i])?String(v[i],4):String("null");}s+=']';};
 array("raw_ut",sensor.lis_raw,fresh);float corrected[]={sensor.lis_x,sensor.lis_y,sensor.lis_z};array("corrected_ut",corrected,fresh&&!sensor.lis_saturated);array("offset_ut",coeff.offset);array("scale",coeff.scale);array("span_ut",r.span);s+=",\"octants\":[";for(unsigned i=0;i<8;i++){if(i)s+=',';s+=String(r.octants[i]);}return s+"]}";
}
bool magWebRoute(const String& path){
 if(!path.startsWith("/api/mag/"))return false;auto& c=i2c_mag_calibration();bool ok=true;magError="";
 if(path=="/api/mag/start"){
  if(bodyActive()){webReply(409,"{\"error\":\"機体姿勢の収録を終了してください\"}");return true;}
  if(!i2c_mag_present())magError="LIS3MDLが未接続または初期化に失敗しています";
  else if(!c.active){runRanging=false;rangeDeadline=0;stopLog();c.start(millis());}
 }else if(path=="/api/mag/save"){
  auto r=c.evaluate(millis());if(!c.active)magError="先に校正を開始してください";
  else if(c.saved){} // Repeated requests do not consume another SD file.
  else if(!r.ready||!i2c_readings().lis_raw_ok||uint32_t(millis()-i2c_readings().lis_sample_ms)>=300)magError=c.saturated?"飽和があり保存できません。磁気源から離して終了・再開始してください":"回転範囲・サンプル数・鮮度が不足しています";
  else if(saveMagCalibration(r.coefficients)){c.saved=true;}
 }else if(path=="/api/mag/end"){c.end();}
 else if(path=="/api/mag/reset"){
  if(c.active)magError="校正を終了してから補正を解除してください";
  else {magcal::Coefficients identity;saveMagCalibration(identity);c.saved=false;}
 }else {magError="未対応の操作です";ok=false;}
 ok=ok&&magError.isEmpty();webReply(ok?200:409,"{\"ok\":"+String(ok?"true":"false")+",\"calibration\":"+magJson()+"}");return true;
}
