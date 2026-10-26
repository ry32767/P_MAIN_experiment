#pragma once
#include "gnss_binary.h"
#include "ntrip_dns.h"
namespace ntrip {
bool online(){return WiFi.getMode()==WIFI_AP_STA && WiFi.connected();}
WiFiClient socket;
String antenna;double antennaHeight=0;bool antennaHeightKnown=false;
String host="ntrip1.bizstation.jp",user,password,ssid,wifiPassword,mount="3C722D56",error="補正開始待ち",header,line,tx;
uint16_t port=2101;uint32_t since=0,lastData=0,lastGga=0,retryAt=0,retries=0,frames=0,bytes=0,tableBytes=0;
unsigned txOffset=0;bool wanted=false,tableMode=false,body=false,firstLine=true,tableReady=false,autoPending=false;
bool deferStart=false;
uint32_t tableGeneration=0;bool historyPending=false,streamRemembered=false;
bool gpsL1Seen=false,gpsL5Seen=false,gpsMsmSeen=false;uint32_t gpsMsmAt=0;
void rememberSuccessfulStream();
gnssbin::Station stations[64],nearest;unsigned count=0,total=0;double nearestDistance=1e100,maxDistance=100000;
gnssbin::RtcmParser parser;bool tableTruncated=false;
enum State {IDLE,WIFI_WAIT,RESOLVE,CONNECT,REQUEST,HEADERS,TABLE,STREAM,BACKOFF};State state=IDLE;
String jsonString(const String& s){String out="\"";for(unsigned i=0;i<s.length();i++){char c=s[i];if(c=='"'||c=='\\')out+='\\';if(uint8_t(c)<32)out+=' ';else out+=c;}return out+'"';}
String b64(const String& s){static const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";String out;for(unsigned i=0;i<s.length();i+=3){uint32_t v=uint8_t(s[i])<<16;if(i+1<s.length())v|=uint8_t(s[i+1])<<8;if(i+2<s.length())v|=uint8_t(s[i+2]);out+=alphabet[(v>>18)&63];out+=alphabet[(v>>12)&63];out+=i+1<s.length()?alphabet[(v>>6)&63]:'=';out+=i+2<s.length()?alphabet[v&63]:'=';}return out;}
bool safe(const String& s,unsigned limit){if(s.length()>limit)return false;for(unsigned i=0;i<s.length();i++)if(uint8_t(s[i])<32||uint8_t(s[i])==127)return false;return true;}
void stop(){deferStart=false;ntripdns::cancel();++correctionGeneration;wanted=false;socket.stop(1);state=IDLE;tx="";autoPending=false;error="停止";parser.reset();}
void fail(const String& why){ntripdns::cancel();socket.stop(1);error=why;parser.reset();tx="";body=false;state=BACKOFF;retryAt=millis()+min(uint32_t(60000),uint32_t(5000*(1u<<min(retries++,unsigned(3)))));}
void start(bool table){streamRemembered=false;gpsL1Seen=gpsL5Seen=gpsMsmSeen=false;deferStart=false;ntripdns::cancel();++correctionGeneration;lastGga=millis()-10000;socket.stop(1);tableMode=table;wanted=true;state=online()?RESOLVE:WIFI_WAIT;since=millis();error="接続待ち";retries=0;body=false;firstLine=true;header=line=tx="";parser.reset();if(table){count=total=tableBytes=0;tableReady=false;tableTruncated=false;nearestDistance=1e100;}}
void selectNearest(){
 auto g=gnssSnapshot();uint64_t now=time_us_64();if(!g.positionValid||now-g.pvtUs>=3000000){error="自動選択には親機GNSSの測位が必要です";wanted=false;state=IDLE;return;}
 if(nearestDistance>maxDistance){error="指定距離内に対応する基準局がありません";wanted=false;state=IDLE;return;}
 mount=nearest.mount;autoPending=false;start(false);
}
void tableLine(){
 if(line=="ENDSOURCETABLE"){++tableGeneration;tableReady=true;socket.stop(1);wanted=false;state=IDLE;error="基準局一覧取得済み";if(autoPending)selectNearest();return;}
 if(!line.startsWith("STR;"))return;
 char buf[1024];line.toCharArray(buf,sizeof(buf));gnssbin::Station s;
 if(!gnssbin::station(buf,s))return;
 ++total;if(count<64)stations[count++]=s;else tableTruncated=true;
 auto g=gnssSnapshot();uint64_t now=time_us_64();if(g.positionValid&&now-g.pvtUs<3000000){double d=gnssbin::distance(g.latE7/1e7,g.lonE7/1e7,s);if(d<nearestDistance){nearestDistance=d;nearest=s;}}
}
void tick(){
 if(!wanted||deferStart)return;uint32_t now=millis();
 if(state==WIFI_WAIT){if(online()){state=RESOLVE;since=now;}else if(uint32_t(now-since)>30000)fail("外部Wi-Fiへ接続できません");return;}
 if(state==BACKOFF){if(int32_t(now-retryAt)>=0){state=online()?RESOLVE:WIFI_WAIT;since=now;}return;}
 if(!online()){fail("外部Wi-Fi接続が途絶えました");return;}
 if(state==RESOLVE){
  if(ntripdns::result==ntripdns::IDLE){error="補正サーバーの名前を確認中";ntripdns::begin(host.c_str(),now);}
  auto resolved=ntripdns::poll(now);
  if(resolved==ntripdns::READY)state=CONNECT;
  else if(resolved==ntripdns::FAILED)fail("補正サーバーの名前解決に失敗しました");
  return;
 }
 if(state==CONNECT){
  if(tableMode){count=total=tableBytes=0;tableReady=false;tableTruncated=false;nearestDistance=1e100;}
  // DNS already completed asynchronously. Give a mobile TCP handshake 2s,
  // then restore short stream writes; never combine DNS and TCP waits.
  socket.setTimeout(2000);bool connected=socket.connect(IPAddress(&ntripdns::address),port);socket.setTimeout(200);
  if(!connected){fail("補正サーバーへTCP接続できません（ポート・回線を確認）");return;}
  gpsL1Seen=gpsL5Seen=gpsMsmSeen=false;streamRemembered=false;
  tx="GET /"+(tableMode?String(""):mount)+" HTTP/1.0\r\nUser-Agent: NTRIP AquaBeacon/1.0\r\nAccept: */*\r\nConnection: close\r\n";
  if(user.length())tx+="Authorization: Basic "+b64(user+":"+password)+"\r\n";
  tx+="\r\n";txOffset=0;state=REQUEST;since=lastData=now;header=line="";firstLine=true;body=false;return;
 }
 if(tx.length()){
  unsigned n=min(unsigned(max(0,socket.availableForWrite())),min(unsigned(256),unsigned(tx.length()-txOffset)));
  if(n)txOffset+=socket.write((const uint8_t*)tx.c_str()+txOffset,n);
  if(txOffset==tx.length()){tx="";txOffset=0;if(state==REQUEST)state=HEADERS;}
  if(uint32_t(now-lastData)>15000)fail("補正接続がタイムアウトしました");return;
 }
 for(unsigned budget=0;budget<2048&&socket.available();budget++){
  uint8_t c=socket.read();lastData=now;
  if(state==HEADERS){
   if(header.length()>=2048){fail("補正サーバーの応答が長すぎます");return;}header+=char(c);
   if(c=='\n'&&firstLine){firstLine=false;String status=header;status.trim();
    if(!status.startsWith("ICY 200")&&!status.startsWith("SOURCETABLE 200")&&!status.startsWith("HTTP/1.0 200")&&!status.startsWith("HTTP/1.1 200")){fail("補正サーバーが接続を拒否しました");wanted=false;return;}
    if(status.startsWith("ICY 200")&&!tableMode){state=STREAM;error="補正受信中";header="";}
   }
   if(header.endsWith("\r\n\r\n")){String lower=header;lower.toLowerCase();if(lower.indexOf("transfer-encoding: chunked")>=0){fail("chunked応答は非対応です。NTRIP v1サービスを選択してください");wanted=false;return;}state=tableMode?TABLE:STREAM;error=tableMode?"基準局一覧取得中":"補正受信中";line="";}
   continue;
  }
  if(state==TABLE){
   if(++tableBytes>262144){fail("基準局一覧のサイズ上限を超えました");wanted=false;return;}
   if(c=='\n'){tableLine();line="";if(!wanted||state!=TABLE)return;}
   else if(c!='\r'){if(line.length()<1023)line+=char(c);else {fail("基準局一覧の行が長すぎます");wanted=false;return;}}continue;
  }
  if(state==STREAM&&parser.push(c)){
   static CorrectionFrame frame;frame.generation=correctionGeneration.load();frame.bytes=parser.bytes;memcpy(frame.data,parser.frame,frame.bytes);
   if(!correctionIn.push(frame)){++correctionTxDropped;fail("GNSS補正送信バッファが満杯です");return;}
   ++frames;bytes+=frame.bytes;
   bool l1=false,l5=false;if(gnssbin::gpsMsmSignals(frame.data,frame.bytes,l1,l5)){gpsMsmSeen=true;gpsMsmAt=now;gpsL1Seen|=l1;gpsL5Seen|=l5;}
   if(!streamRemembered){streamRemembered=true;rememberSuccessfulStream();}
   if(gnssRecording.load()&&!correctionLogs.push(frame))++correctionLogDropped;
  }
 }
 if(state==STREAM&&uint32_t(now-lastGga)>=10000){auto g=gnssSnapshot();if(g.gga[0]&&time_us_64()-g.ggaUs<3000000){tx=String(g.gga)+"\r\n";txOffset=0;lastGga=now;}}
 if(!socket.connected()&&!socket.available()){fail("補正サーバーとの接続が終了しました");return;}
 if(uint32_t(now-lastData)>15000)fail("補正データの更新が停止しました");
}
}
String stationJson(const gnssbin::Station& s){
 auto g=gnssSnapshot();bool fresh=g.positionValid&&time_us_64()-g.pvtUs<3000000;
 return "{\"mount\":"+ntrip::jsonString(s.mount)+",\"name\":"+ntrip::jsonString(s.name)+",\"format\":"+ntrip::jsonString(s.format)+",\"details\":"+ntrip::jsonString(s.details)+",\"systems\":"+ntrip::jsonString(s.systems)+",\"network\":"+ntrip::jsonString(s.network)+",\"lat\":"+String(s.lat,7)+",\"lon\":"+String(s.lon,7)+",\"distance_m\":"+(fresh?String(gnssbin::distance(g.latE7/1e7,g.lonE7/1e7,s),1):String("null"))+",\"gga\":"+String(s.gga?"true":"false")+"}";
}
String rtkSummaryJson(){
 using namespace ntrip;bool signalFresh=state==STREAM&&gpsMsmSeen&&uint32_t(millis()-gpsMsmAt)<15000;
 return "{\"table_generation\":"+String(tableGeneration)+",\"gps_msm_fresh\":"+String(signalFresh?"true":"false")+",\"gps_l1_seen\":"+String(gpsL1Seen?"true":"false")+",\"gps_l5_seen\":"+String(gpsL5Seen?"true":"false")+",\"antenna\":"+jsonString(antenna)+",\"antenna_height_m\":"+(antennaHeightKnown?String(antennaHeight,4):String("null"))+",\"state\":"+jsonString(error)+",\"connected\":"+String(state==STREAM?"true":"false")+",\"auto_pending\":"+String(autoPending?"true":"false")+",\"wifi_connected\":"+String(online()?"true":"false")+",\"mount\":"+jsonString(mount)+",\"host\":"+jsonString(host)+",\"port\":"+String(port)+",\"rtcm_frames\":"+String(frames)+",\"rtcm_bytes\":"+String(bytes)+",\"rtcm_crc_errors\":"+String(parser.errors)+",\"rtcm_tx_frames\":"+String(correctionTxFrames.load())+",\"rtcm_tx_dropped\":"+String(correctionTxDropped.load())+",\"rtcm_log_dropped\":"+String(correctionLogDropped.load())+",\"table_ready\":"+String(tableReady?"true":"false")+",\"table_truncated\":"+String(tableTruncated?"true":"false")+",\"stations_total\":"+String(total)+",\"nearest_distance_m\":"+(nearestDistance<1e99?String(nearestDistance,1):String("null"))+"}";
}
String rtkJson(){String out=rtkSummaryJson();out.remove(out.length()-1);out+=",\"stations\":[";for(unsigned i=0;i<ntrip::count;i++){if(i)out+=',';out+=stationJson(ntrip::stations[i]);}return out+"]}";}
