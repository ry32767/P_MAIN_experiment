#pragma once
FsFile gnssFile,rtcmFile,gnssMetaFile;
char gnssName[64]{},rtcmName[64]{},gnssMetaName[64]{};
uint32_t gnssSavedRawx=0,gnssSavedSfrbx=0,gnssSavedBytes=0,rtcmSavedBytes=0;
uint32_t gnssSessionRawxDrops=0,gnssSessionSfrbxDrops=0,gnssSessionDiagnosticDrops=0;
uint32_t gnssSessionDrops=0,gnssSessionUartDrops=0,gnssSessionErrors=0,lastGnssMetaMs=0;
String int64Text(int64_t);
String rtkSummaryJson();String gnssJson(const GnssStatus&,uint64_t);
String gnssStorageDiagnosticsJson(){return ",\"queue_bytes\":"+String(gnssFrames.queued())+",\"queue_high_water_bytes\":"+String(gnssFrames.highWater.load())+",\"enqueue_dropped_rawx\":"+String(gnssRawxDropped.load()-gnssSessionRawxDrops)+",\"enqueue_dropped_sfrbx\":"+String(gnssSfrbxDropped.load()-gnssSessionSfrbxDrops)+",\"enqueue_dropped_other\":"+String(gnssDiagnosticDropped.load()-gnssSessionDiagnosticDrops);}
void closeGnssFiles(){gnssRecording=false;if(gnssFile)gnssFile.close();if(rtcmFile)rtcmFile.close();if(gnssMetaFile)gnssMetaFile.close();}
bool syncGnssFiles(){return (!gnssFile||gnssFile.sync())&&(!rtcmFile||rtcmFile.sync())&&(!gnssMetaFile||gnssMetaFile.sync());}
bool openGnssFiles(){
 for(unsigned i=0;i<100000;i++){
  snprintf(gnssName,sizeof(gnssName),"P_MAIN_GNSS_%05u.ubx",i);snprintf(rtcmName,sizeof(rtcmName),"P_MAIN_GNSS_%05u.rtcm3",i);snprintf(gnssMetaName,sizeof(gnssMetaName),"P_MAIN_GNSS_%05u.jsonl",i);
  if(sd.exists(gnssName)||sd.exists(rtcmName)||sd.exists(gnssMetaName))continue;
  gnssFile=sd.open(gnssName,O_WRONLY|O_CREAT|O_EXCL);rtcmFile=sd.open(rtcmName,O_WRONLY|O_CREAT|O_EXCL);gnssMetaFile=sd.open(gnssMetaName,O_WRONLY|O_CREAT|O_EXCL);
  if(!gnssFile||!rtcmFile||!gnssMetaFile){closeGnssFiles();return false;}
  gnssSavedRawx=gnssSavedSfrbx=gnssSavedBytes=rtcmSavedBytes=0;gnssSessionDrops=gnssLogDropped.load();gnssSessionRawxDrops=gnssRawxDropped.load();gnssSessionSfrbxDrops=gnssSfrbxDropped.load();gnssSessionDiagnosticDrops=gnssDiagnosticDropped.load();auto g=gnssSnapshot();gnssSessionUartDrops=g.uartOverflows;gnssSessionErrors=g.ubxErrors;lastGnssMetaMs=0;return true;
 }return false;
}
void gnssStorageTick(){
 static GnssFrame frame;static CorrectionFrame corr;uint64_t began=time_us_64();
 for(unsigned budget=0;budget<16 && (budget==0||time_us_64()-began<4000) && gnssFrames.pop(frame);budget++){
  if(!sdHealthy||!gnssFile){++gnssLogDropped;continue;}
  if(gnssFile.write(frame.data,frame.bytes)!=frame.bytes||gnssFile.getWriteError()){failSd("GNSS_WRITE_FAILED");return;}
  gnssSavedBytes+=frame.bytes;logGnssObservation(frame);if(!sdHealthy)return;
  if(frame.data[2]==2&&frame.data[3]==0x15)++gnssSavedRawx;
  if(frame.data[2]==2&&frame.data[3]==0x13)++gnssSavedSfrbx;
 }
 for(unsigned budget=0;budget<8&&correctionLogs.pop(corr);budget++){
  if(!sdHealthy||!rtcmFile){++correctionLogDropped;continue;}
  if(rtcmFile.write(corr.data,corr.bytes)!=corr.bytes||rtcmFile.getWriteError()){failSd("RTCM_WRITE_FAILED");return;}rtcmSavedBytes+=corr.bytes;
 }
 if(sdHealthy&&gnssMetaFile&&(!lastGnssMetaMs||uint32_t(millis()-lastGnssMetaMs)>=5000)){
  lastGnssMetaMs=millis();uint64_t now=time_us_64();String line="{\"session\":"+String(sessionId)+",\"mono_us\":"+int64Text(now)+",\"gnss\":"+gnssJson(gnssSnapshot(),now)+",\"rtk\":"+rtkSummaryJson()+",\"rawx_saved\":"+String(gnssSavedRawx)+",\"sfrbx_saved\":"+String(gnssSavedSfrbx)+",\"dropped\":"+String(gnssLogDropped.load()-gnssSessionDrops)+gnssStorageDiagnosticsJson()+"}\n";
  if(gnssMetaFile.write(line.c_str(),line.length())!=line.length()||gnssMetaFile.getWriteError())failSd("GNSS_META_WRITE_FAILED");
 }
}
