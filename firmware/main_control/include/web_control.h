#pragma once
WiFiServer server(80);WiFiClient client;
String request,response;size_t responseSent=0;uint32_t clientSince=0;
const char apPassword[]="Aqua@1234";
static bool webWaiting=false,webRemote=false;
static uint32_t webSeq=0,webNextSeq=0,rangeDeadline=0;
static String requestOrigin(){
 String lower=request;lower.toLowerCase();int i=lower.indexOf("\r\norigin:");if(i<0)return "";int end=request.indexOf("\r\n",i+2);String origin=request.substring(i+9,end);origin.trim();return origin;
}
static bool allowedOrigin(const String& origin){return origin.length()==0 || origin=="http://192.168.4.1" || origin=="https://ry32767.github.io";}
static void webReply(int code,const String& body,const char* type="application/json"){
 String cors;auto origin=requestOrigin();
 if(origin=="https://ry32767.github.io")cors="Access-Control-Allow-Origin: https://ry32767.github.io\r\nVary: Origin\r\nAccess-Control-Allow-Methods: GET, POST, OPTIONS\r\nAccess-Control-Allow-Headers: X-Aqua-Control\r\nAccess-Control-Allow-Private-Network: true\r\nAccess-Control-Max-Age: 600\r\n";
 response="HTTP/1.1 "+String(code)+" OK\r\n"+cors+"Content-Type: "+type+"\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: "+String(body.length())+"\r\n\r\n"+body;responseSent=0;
}
static String controlJson(const aq::Control& c){
 String hex;hex.reserve(c.length*2);
 static const char digits[]="0123456789abcdef";
 for(unsigned i=0;i<c.length && i<sizeof(c.data);i++){hex+=digits[c.data[i]>>4];hex+=digits[c.data[i]&15];}
 return "{\"error\":"+String(c.error)+",\"arg\":"+String(c.arg)+",\"length\":"+String(c.length)+",\"hex\":\""+hex+"\"}";
}
void wifiSetup(){
 WiFi.mode(WIFI_AP);if(WiFi.softAP("AquaBeacon-MAIN",apPassword)){server.begin();Serial.printf("# WIFI ssid=AquaBeacon-MAIN password=%s ip=%s\n",apPassword,WiFi.softAPIP().toString().c_str());}
}
static void webRoute(){
 String method=request.substring(0,request.indexOf(' '));int end=request.indexOf(' ',method.length()+1);String path=request.substring(method.length()+1,end);
 if(!allowedOrigin(requestOrigin())){webReply(403,"{\"error\":\"origin not allowed\"}");return;}
 if(method=="OPTIONS"){webReply(204,"");return;}
 if(method=="GET" && path=="/"){webReply(200,WEB_PAGE,"text/html; charset=utf-8");return;}
 if(method=="GET" && path=="/api/status"){webReply(200,statusJson());return;}
 String lower=request;lower.toLowerCase();
 if(method!="POST" || lower.indexOf("\r\nx-aqua-control: 1\r\n")<0){webReply(403,"{\"error\":\"control header required\"}");return;}
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
 if(path=="/api/range/start" || path=="/api/range/test"){
  mutex_enter_blocking(&stateMutex);bool linked=syncOk && remoteSeenMs && millis()-remoteSeenMs<3000;mutex_exit(&stateMutex);
  if(!linked){webReply(409,"{\"error\":\"子機との同期を確認してください\"}");return;}
  runRanging=true;rangeDeadline=path.endsWith("test")?millis()+60000:0;webReply(200,"{\"ok\":true}");return;
 }
 // Numeric operations remain internal; the UI exposes descriptive Japanese labels.
 if(!path.startsWith("/api/control/")){webReply(404,"{\"error\":\"not found\"}");return;}
 String tail=path.substring(13);int slash=tail.indexOf('/');String target=tail.substring(0,slash);tail=tail.substring(slash+1);
 int sep=tail.indexOf('/');if(slash<0 || sep<0 || (target!="p"&&target!="c")){webReply(400,"{\"error\":\"bad request\"}");return;}
 aq::Control c;c.op=tail.substring(0,sep).toInt();tail=tail.substring(sep+1);sep=tail.indexOf('/');
 String number=sep<0?tail:tail.substring(0,sep);c.arg=strtoul(number.c_str(),nullptr,10);
 if(sep>=0){String name=tail.substring(sep+1);if(name.length()>=64){webReply(400,"{\"error\":\"filename too long\"}");return;}name.toCharArray((char*)c.data,sizeof(c.data));}
 if(c.op<aq::INFO || c.op>aq::RX_SAMPLE){webReply(400,"{\"error\":\"bad operation\"}");return;}
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
 if(!client){
  if(webWaiting)return;
  client=server.accept();if(!client)return;clientSince=millis();request="";response="";responseSent=0;
 }
 if(millis()-clientSince>7000){client.stop();return;}
 if(webWaiting)return;
 if(response.length()){
  size_t n=min(size_t(client.availableForWrite()),min(size_t(512),response.length()-responseSent));
  if(n)responseSent+=client.write((const uint8_t*)response.c_str()+responseSent,n);
  if(responseSent==response.length())client.stop();return;
 }
 for(unsigned n=0;n<256 && client.available();n++){
  request+=char(client.read());if(request.length()>1536){client.stop();return;}
  if(request.endsWith("\r\n\r\n")){webRoute();return;}
 }
}
