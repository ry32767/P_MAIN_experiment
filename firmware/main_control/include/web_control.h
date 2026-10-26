#pragma once
#include <pico/cyw43_arch.h>
WiFiServer server(80);WiFiClient client;
String request,response;size_t responseSent=0;uint32_t clientSince=0,clientProgress=0;
static const char* responseStaticBody=nullptr;
static size_t responseStaticSize=0;
static size_t webResponseLength(){return response.length()+responseStaticSize;}
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef AQUA_AP_PASSWORD
#define AQUA_AP_PASSWORD "CHANGE-ME-BEFORE-USE"
#endif
const char apPassword[]=AQUA_AP_PASSWORD;
static bool webWaiting=false,webRemote=false;
static uint32_t webSeq=0,webNextSeq=0,rangeDeadline=0;
static bool webApStarted=false;
struct WebDiagnostics {
 uint32_t accepted=0,requests=0,received=0,sent=0,completed=0,timeouts=0;
 uint32_t pageRequests=0,pageCompleted=0,pingRequests=0,statusRequests=0;
 int replyCode=0;
};
static WebDiagnostics webDiag;
// USB-only diagnostics: no request contents, credentials or client identities.
static String webDiagnosticsJson(){
 String extra=String(",\"sta_connected\":")+String(ntrip::online()?"true":"false")+",\"sta_ip\":\""+WiFi.localIP().toString()+"\"";
 extra+=",\"sta_status\":"+String(unsigned(WiFi.status()))+",\"wifi_configured\":"+String(ntrip::ssid.length()?"true":"false")+",\"wifi_attempts\":"+String(ntrip::wifiJoinAttempts);
 extra+=",\"driver_link_status\":"+String(cyw43_wifi_link_status(&cyw43_state,CYW43_ITF_STA))+",\"driver_join_state\":"+String(cyw43_state.wifi_join_state)+",\"join_age_ms\":"+String(uint32_t(millis()-ntrip::wifiLastAttempt));
 extra+=",\"page_requests\":"+String(webDiag.pageRequests)+",\"page_completed\":"+String(webDiag.pageCompleted)+",\"ping_requests\":"+String(webDiag.pingRequests)+",\"status_requests\":"+String(webDiag.statusRequests)+",\"ap_mode_enabled\":"+String(WiFi.getMode()==WIFI_AP||WiFi.getMode()==WIFI_AP_STA?"true":"false");
 return String("{\"web_diag\":true")+extra+",\"ap_started\":"+String(webApStarted?"true":"false")+",\"mode\":"+String(unsigned(WiFi.getMode()))+",\"ap_ip\":\""+WiFi.softAPIP().toString()+"\",\"stations\":"+String(WiFi.softAPgetStationNum())+",\"listening\":"+String(server?"true":"false")+",\"accepted\":"+String(webDiag.accepted)+",\"requests\":"+String(webDiag.requests)+",\"rx_bytes\":"+String(webDiag.received)+",\"tx_bytes\":"+String(webDiag.sent)+",\"completed\":"+String(webDiag.completed)+",\"timeouts\":"+String(webDiag.timeouts)+",\"last_code\":"+String(webDiag.replyCode)+",\"response_bytes\":"+String(webResponseLength())+",\"response_sent\":"+String(responseSent)+",\"heap_free\":"+String(rp2040.getFreeHeap())+",\"waiting_control\":"+String(webWaiting?"true":"false")+"}";
}
static String requestOrigin(){
 String lower=request;lower.toLowerCase();int i=lower.indexOf("\r\norigin:");if(i<0)return "";int end=request.indexOf("\r\n",i+2);String origin=request.substring(i+9,end);origin.trim();return origin;
}
static bool allowedOrigin(const String& origin){
 if(origin.length()==0||origin=="https://ry32767.github.io")return true;
 if(origin=="http://"+WiFi.softAPIP().toString())return true;
 return WiFi.connected()&&WiFi.localIP().isSet()&&origin=="http://"+WiFi.localIP().toString();
}
static void webReplyHeader(int code,size_t length,const char* type){
 webDiag.replyCode=code;
 String cors;auto origin=requestOrigin();
 if(origin=="https://ry32767.github.io")cors="Access-Control-Allow-Origin: https://ry32767.github.io\r\nVary: Origin\r\nAccess-Control-Allow-Methods: GET, POST, OPTIONS\r\nAccess-Control-Allow-Headers: X-Aqua-Control\r\nAccess-Control-Allow-Private-Network: true\r\nAccess-Control-Max-Age: 600\r\n";
 response="HTTP/1.1 "+String(code)+" OK\r\n"+cors+"Content-Type: "+type+"\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: "+String(length)+"\r\n\r\n";responseSent=0;responseStaticBody=nullptr;responseStaticSize=0;
}
static void webReply(int code,const String& body,const char* type="application/json"){
 webReplyHeader(code,body.length(),type);response+=body;
}
static void webReplyPage(){
 // Keep the page in flash: constructing String(WEB_PAGE) plus a full HTTP
 // response needs multiple large contiguous heap allocations and can fail.
 webReplyHeader(200,sizeof(WEB_PAGE)-1,"text/html; charset=utf-8");
 responseStaticBody=WEB_PAGE;responseStaticSize=sizeof(WEB_PAGE)-1;
}
static String controlJson(const aq::Control& c){
 String hex;hex.reserve(c.length*2);
 static const char digits[]="0123456789abcdef";
 for(unsigned i=0;i<c.length && i<sizeof(c.data);i++){hex+=digits[c.data[i]>>4];hex+=digits[c.data[i]&15];}
 return "{\"error\":"+String(c.error)+",\"arg\":"+String(c.arg)+",\"length\":"+String(c.length)+",\"hex\":\""+hex+"\"}";
}
#include "gnss_web.h"
#include "mag_console.h"
#include "parent_calibration_web.h"
void wifiSetup(){
 // softAPConfig selects the STA configuration unless AP mode is set first.
 WiFi.mode(WIFI_AP);
 WiFi.softAPConfig(IPAddress(192,168,4,1),IPAddress(192,168,4,1),IPAddress(255,255,255,0));
 webApStarted=WiFi.softAP("AquaBeacon-MAIN",apPassword);if(webApStarted){server.begin();Serial.printf("# WIFI ssid=AquaBeacon-MAIN password=%s ip=%s\n",apPassword,WiFi.softAPIP().toString().c_str());}
}
static void webRoute(){
 ++webDiag.requests;
 String method=request.substring(0,request.indexOf(' '));int end=request.indexOf(' ',method.length()+1);String path=request.substring(method.length()+1,end);
 if(!allowedOrigin(requestOrigin())){webReply(403,"{\"error\":\"origin not allowed\"}");return;}
 if(method=="OPTIONS"){webReply(204,"");return;}
 if(method=="GET" && path=="/"){++webDiag.pageRequests;webReplyPage();return;}
 if(method=="GET" && path=="/api/ping"){++webDiag.pingRequests;webReply(200,"{\"ok\":true}");return;}
 if(method=="GET" && path=="/api/gnss/stations"){webReply(200,rtkJson());return;}
 if(method=="GET" && path=="/api/gnss/config"){webReply(200,ntrip::settingsJson());return;}
 if(method=="GET" && path=="/api/parent-calibration"){webReply(200,parentCalibrationJson());return;}
 if(method=="GET" && path=="/api/status"){++webDiag.statusRequests;webReply(200,statusJson());return;}
 String lower=request;lower.toLowerCase();
 if(method!="POST" || lower.indexOf("\r\nx-aqua-control: 1\r\n")<0){webReply(403,"{\"error\":\"control header required\"}");return;}
 if(path.startsWith("/api/spresense/")) {
  String op=path.substring(15);uint8_t command=op=="stop"?sprctl::STOP:op=="start"?sprctl::START:op=="reset"?sprctl::RESET:op=="calibration/start"?sprctl::CAL_START:op=="calibration/status"?sprctl::CAL_STATUS:op=="calibration/clear"?sprctl::CAL_CLEAR:op=="calibration/save"?sprctl::CAL_SAVE:op=="calibration/end"?sprctl::CAL_END:op.startsWith("calibration/pose/")?sprctl::CAL_POSE:0;
  if(!command){webReply(404,"{\"error\":\"unknown Spresense operation\"}");return;}
  auto s=spresenseSnapshot();
  if(!s.enabled || !spresenseFresh(s,time_us_64())){webReply(409,"{\"error\":\"Spresenseが応答していません\"}");return;}
  if(bodyActive()&&command>=sprctl::CAL_START&&command!=sprctl::CAL_STATUS){webReply(409,"{\"error\":\"機体姿勢の収録中は個別校正を変更できません\"}");return;}
  if(command==sprctl::RESET && (s.recording || s.lastOperation!=sprctl::STOP || s.lastResult!=sprctl::OK)){webReply(409,"{\"error\":\"Spresenseの記録を停止し、停止完了を確認してください\"}");return;}
  if(command==sprctl::CAL_CLEAR){int pos=request.indexOf("\r\n\r\n");String body=pos>=0?request.substring(pos+4):String("");if(formField(body,"confirm_disable")!="1"){webReply(409,"{\"error\":\"補正を無効にして保存する確認が必要です\"}");return;}}
  float gravity=9.80665f,latitude=0;uint8_t pose=255;
  if(command==sprctl::CAL_START){
   int pos=request.indexOf("\r\n\r\n");String body=pos>=0?request.substring(pos+4):String("");
   double g=0,l=0;auto gs=formField(body,"gravity"),ls=formField(body,"latitude");
   if(gs.isEmpty()||ls.isEmpty()||!gnssbin::coordinate(gs.c_str(),g,10)||!gnssbin::coordinate(ls.c_str(),l,90)||!sprcal::config(g,l)){webReply(400,"{\"error\":\"重力値と緯度を確認してください\"}");return;}
   gravity=g;latitude=l;
  }
  if(command==sprctl::CAL_POSE){auto value=op.substring(17);if(value.length()!=1||value[0]<'0'||value[0]>'5'){webReply(400,"{\"error\":\"姿勢番号が無効です\"}");return;}pose=value[0]-'0';}
  if(command>=sprctl::CAL_START && command!=sprctl::CAL_STATUS && s.recording){webReply(409,"{\"error\":\"Spresenseの記録を停止してください\"}");return;}
  uint32_t id=0;if(!requestSpresense(command,id,gravity,latitude,pose)){webReply(409,"{\"error\":\"Spresenseの操作を処理中です\"}");return;}
  webReply(202,"{\"accepted\":true,\"command_id\":"+String(id)+"}");return;
 }
 if(parentCalibrationWebRoute(path))return;
 if(magWebRoute(path))return;
 if(gnssWebRoute(path))return;
 if(path=="/api/phone/probe"){auto now=time_us_64()/1000;auto token=phoneReference.probe(now);webReply(200,"{\"session\":"+String(sessionId)+",\"token\":"+String(token)+"}");return;}
 if(path=="/api/phone/clear"){phoneReference.clear();webReply(200,"{\"ok\":true}");return;}
 if(path.startsWith("/api/phone/time/") || path.startsWith("/api/phone/position/")){
  bool time=path.startsWith("/api/phone/time/");int64_t v[5]{};auto tail=path.substring(time?16:20);
  bool ok=phone::numbers(tail.c_str(),v,time?4:5) && v[0]==int64_t(sessionId);
  if(ok && time)ok=v[1]>=0 && v[1]<=UINT32_MAX && phoneReference.setTime(uint32_t(v[1]),v[2],v[3],time_us_64()/1000);
  else if(ok)ok=phoneReference.position(v[1],v[2],v[3],v[4],time_us_64()/1000);
  webReply(ok?200:400,ok?"{\"ok\":true}":"{\"error\":\"時刻・位置データが無効または期限切れです\"}");return;
 }
 if(path=="/api/range/stop"){runRanging=false;rangeDeadline=0;webReply(200,"{\"ok\":true}");return;}
 if(path.startsWith("/api/range/config/")){
  if(runRanging){webReply(409,"{\"error\":\"測距送信を停止してから設定してください\"}");return;}
  int64_t v[5]{};String values=path.substring(18);unsigned fields=0;for(unsigned i=0;i<values.length();i++)if(values[i]=='/')fields++;bool extended=fields==4;bool ok=phone::numbers(path.substring(18).c_str(),v,extended?5:3) && v[0]>0 && v[0]<=UINT32_MAX && v[1]>=1 && v[1]<=5 && v[2]>=0 && v[2]<=300 && (!extended || (v[3]>=1 && v[3]<=4 && v[4]>=0 && v[4]<=1));
  aq::AcquisitionConfig c;if(ok){c={uint32_t(v[0]),uint16_t(v[1]),uint16_t(v[2]),uint8_t(extended?v[3]:1),uint8_t(extended?v[4]:0)};ok=aq::validConfig(c);}
  if(ok){mutex_enter_blocking(&stateMutex);acquisitionConfig=c;mutex_exit(&stateMutex);}
  webReply(ok?200:400,ok?"{\"ok\":true}":"{\"error\":\"周波数・観測時間・記録頻度・回数・パターンを確認してください\"}");return;
 }
 if(path=="/api/range/start" || path=="/api/range/test" || path=="/api/range/start/ack"){
  if(bodyActive()){webReply(409,"{\"error\":\"機体姿勢の収録を終了してください\"}");return;}
  if(i2c_mag_calibration().active){webReply(409,"{\"error\":\"親機磁気校正を終了してください\"}");return;}
  mutex_enter_blocking(&stateMutex);bool linked=remoteSeenMs && millis()-remoteSeenMs<3000;mutex_exit(&stateMutex);
  if(!linked){webReply(409,"{\"error\":\"子機との接続を確認してください\"}");return;}
  mutex_enter_blocking(&stateMutex);bool calibrating=remoteHealth.flags&aq::CALIBRATING;mutex_exit(&stateMutex);
  if(calibrating){webReply(409,"{\"error\":\"子機の校正を終了してから測距を開始してください\"}");return;}
  mutex_enter_blocking(&stateMutex);bool remoteCamera=remoteHealth.flags&aq::CAMERA_READY;mutex_exit(&stateMutex);
  if(!bodyCameraReady()||!remoteCamera){webReply(409,"{\"error\":\"今回の撮影用の動作収録が未完了です。親機・子機それぞれ30秒の動作収録を完了してください。省略はできません\",\"camera_motion_required\":true}");return;}
  if(!path.endsWith("/ack")&&!bodyExperimentReady()){webReply(409,"{\"error\":\"個別校正・機体の軸合わせが未完了です。警告を確認して開始してください\",\"calibration_warning\":true}");return;}
  runRanging=true;rangeDeadline=path.endsWith("test")?millis()+60000:0;webReply(200,"{\"ok\":true}");return;
 }
 // Numeric operations remain internal; the UI exposes descriptive Japanese labels.
 if(!path.startsWith("/api/control/")){webReply(404,"{\"error\":\"not found\"}");return;}
 String tail=path.substring(13);int slash=tail.indexOf('/');String target=tail.substring(0,slash);tail=tail.substring(slash+1);
 int sep=tail.indexOf('/');if(slash<0 || sep<0 || (target!="p"&&target!="c")){webReply(400,"{\"error\":\"bad request\"}");return;}
 aq::Control c;c.op=tail.substring(0,sep).toInt();tail=tail.substring(sep+1);sep=tail.indexOf('/');
 String number=sep<0?tail:tail.substring(0,sep);c.arg=strtoul(number.c_str(),nullptr,10);
 if(sep>=0&&c.op==aq::BODY_IMPORT){
  String data=tail.substring(sep+1);if(data.length()!=sizeof(bodycal::Solution)*2){webReply(400,"{\"error\":\"校正結果の長さが不正です\"}");return;}
  for(unsigned i=0;i<data.length();i++){char x=data[i];if(!isxdigit(x)){webReply(400,"{\"error\":\"校正結果が不正です\"}");return;}}
  for(unsigned i=0;i<sizeof(bodycal::Solution);i++){String pair=data.substring(i*2,i*2+2);c.data[i]=strtoul(pair.c_str(),nullptr,16);}c.length=sizeof(bodycal::Solution);
 }else if(sep>=0){String name=tail.substring(sep+1);if(name.length()>=64){webReply(400,"{\"error\":\"filename too long\"}");return;}name.toCharArray((char*)c.data,sizeof(c.data));}
 if(c.op<aq::INFO || c.op>aq::BODY_IMPORT){webReply(400,"{\"error\":\"bad operation\"}");return;}
 if((c.op==aq::READ_LOG || c.op==aq::FILE_LIST) && runRanging){webReply(409,"{\"error\":\"測距を停止してから回収してください\"}");return;}
 webRemote=target=="c";
 if(webRemote){
  mutex_enter_blocking(&stateMutex);bool linked=remoteSeenMs && millis()-remoteSeenMs<3000;mutex_exit(&stateMutex);
  if(!linked){webReply(409,"{\"error\":\"子機が応答していません\"}");return;}
  aq::Packet p;p.kind=aq::CONTROL;p.session=sessionId;p.seq=webSeq=++webNextSeq;aq::pack(p,c);
  if(!controlOut.push(p)){webReply(409,"{\"error\":\"処理中です\"}");return;}
 }else if(!beginControl(c)){webReply(409,"{\"error\":\"処理中です\"}");return;}
 webWaiting=true;
}
void webTick(){
 if(rangeDeadline && int32_t(millis()-rangeDeadline)>=0){runRanging=false;rangeDeadline=0;}
 // Finish accepted operations even if the requesting browser has disconnected.
 if(webWaiting){
  if(webRemote){aq::Packet p;while(controlIn.pop(p)){if(p.seq==webSeq){webReply(200,controlJson(aq::unpack(p)));webWaiting=false;break;}}}
  else if(localControlDone){webReply(200,controlJson(localAnswer));localControlDone=false;webWaiting=false;}
 }
 // Apply Wi-Fi changes only after the HTTP reply has left our send queue.
 // A channel change can still require the phone to rejoin AquaBeacon-MAIN.
 if(!client&&!webWaiting)ntrip::wifiSettingsTick();
 if(!client&&!webWaiting)ntrip::deferStart=false;
 if(!client){
  if(webWaiting)return;
  client=server.accept();if(!client)return;client.setNoDelay(true);clientSince=clientProgress=millis();request="";response="";responseSent=0;responseStaticBody=nullptr;responseStaticSize=0;
  ++webDiag.accepted;
 }
 // Browser preconnect sockets carry no HTTP bytes. Do not let one occupy our
 // only request slot longer than the UI's operation timeout.
 if(!webWaiting&&!request.length()&&!response.length()&&!client.available()&&millis()-clientSince>1000){++webDiag.timeouts;client.stop(1);return;}
 // A large page may take longer than seven seconds on a slow phone link.
 // Bound inactivity separately from total lifetime so progress is not truncated.
 if(millis()-clientProgress>7000 || millis()-clientSince>120000){++webDiag.timeouts;client.stop();return;}
 if(webWaiting)return;
 if(response.length()){
  bool header=responseSent<response.length();
  const char* data=header?response.c_str()+responseSent:responseStaticBody+(responseSent-response.length());
  size_t remaining=header?response.length()-responseSent:webResponseLength()-responseSent;
  size_t n=min(size_t(client.availableForWrite()),min(size_t(1460),remaining));
  if(n){size_t written=client.write((const uint8_t*)data,n);responseSent+=written;webDiag.sent+=written;if(written)clientProgress=millis();}
  if(responseSent==webResponseLength()){++webDiag.completed;if(responseStaticBody)++webDiag.pageCompleted;client.stop();}return;
 }
 for(unsigned n=0;n<256 && client.available();n++){
  request+=char(client.read());++webDiag.received;clientProgress=millis();if(request.length()>4096){client.stop();return;}
  int headers=request.indexOf("\r\n\r\n");
  if(headers>=0){String lower=request.substring(0,headers);lower.toLowerCase();int i=lower.indexOf("\r\ncontent-length:");unsigned length=0;if(i>=0){int end=lower.indexOf("\r\n",i+2);if(end<0)end=lower.length();String raw=lower.substring(i+17,end);raw.trim();for(unsigned j=0;j<raw.length();j++)if(raw[j]<'0'||raw[j]>'9'){client.stop();return;}length=raw.toInt();if(length>2048){client.stop();return;}}if(request.length()>=unsigned(headers+4)+length){webRoute();return;}}

 }
}
