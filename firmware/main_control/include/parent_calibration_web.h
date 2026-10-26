#pragma once
static bool parentCalibrationCanSave(){
 auto sp=spresenseSnapshot();bool fresh=spresenseFresh(sp,time_us_64());
 return stopped&&!stopRequested&&!runRanging&&!dumping&&!localControlBusy&&gnssSettingsStorageReady()&&!i2c_mag_calibration().active&&
  (!fresh||(!sp.recording&&!sp.commandPending&&!(sp.calibration>=1&&sp.calibration<=4)));
}
static void parentCalibrationReferenceCurrent(parentcal::Record& c){
 auto sp=spresenseSnapshot();bool fresh=spresenseFresh(sp,time_us_64());
 c.imuCalibration=fresh&&sp.calibrationApplied?sp.calibrationGeneration:0;
 c.magCalibration=i2c_mag_coefficients().enabled?magRecordSequence:0;
}
static bool parentCalibrationWebRoute(const String& path){
 if(path!="/api/parent-calibration/save")return false;
 auto result=[](bool ok,const String& message){parentCalibrationError=ok?String(""):message;webReply(ok?200:409,"{\"ok\":"+String(ok?"true":"false")+",\"message\":"+ntrip::jsonString(message)+"}");};
 if(!parentCalibrationCanSave()){
  result(false,"親機・Spresenseの記録と測距・個別校正を停止してください。保存には親機SDが必要です");return true;
 }
 int pos=request.indexOf("\r\n\r\n");String body=pos>=0?request.substring(pos+4):String("");parentcal::Record c;
 if(formField(body,"reference_only")=="1"){
  // Update calibration versions without replacing saved geometry with defaults.
  if(parentCalibrationLoaded)c=parentCalibration;
  parentCalibrationReferenceCurrent(c);
  if(!saveParentCalibration(c)){result(false,"校正セットの保存・読戻しに失敗しました。以前のセットを維持します");return true;}
  result(true,"現在の個別校正をセットに保存しました。保存番号と個別校正の一致を確認してください");return true;
 }
 auto field=[&](const char* key,char* destination){String s=formField(body,key);if(s.length()>=40 || !ntrip::safe(s,39))return false;s.toCharArray(destination,40);return parentcal::text(destination,40);};
 bool valid=field("name",c.name)&&field("cad",c.cad)&&field("body",c.body);
 for(unsigned i=0;i<3;i++){
  String prefix="s"+String(i);valid=field((prefix+"id").c_str(),c.sensor[i])&&valid;
  auto k=formField(body,(prefix+"known").c_str());if(k!="0"&&k!="1")valid=false;c.mounts[i].known=k=="1";
  if(!c.mounts[i].known)continue;
  for(unsigned j=0;j<7;j++){
   String key=prefix+String(j);auto value=formField(body,key.c_str());double number=0;
   if(value.isEmpty()||!gnssbin::coordinate(value.c_str(),number,j<3?100:1))valid=false;
   else if(j<3)c.mounts[i].position[j]=number;else c.mounts[i].q[j-3]=number;
  }
  if(!parentcal::mountValid(c.mounts[i]))valid=false;
 }
 if(!valid){result(false,"名称はUTF-8で39バイト以内、位置はm、姿勢は正規化した四元数w,x,y,zで入力してください");return true;}
 // Store exact individual calibration versions; an unavailable IMU stays unknown.
 parentCalibrationReferenceCurrent(c);
 if(!saveParentCalibration(c)){result(false,"校正セットの保存・読戻しに失敗しました。以前のセットを維持します");return true;}
 result(true,parentcal::geometryComplete(c)?"親機校正セットを保存・適用しました。個別校正との一致とPPS状態を確認してください":"未設定を含む下書きとして保存しました。CAD配置・識別名を登録してください");return true;
}
