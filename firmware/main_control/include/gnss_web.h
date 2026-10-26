#pragma once
// Form fields travel in POST bodies; secrets never enter URLs, status or logs.
static String formDecode(const String& text){String s;for(unsigned i=0;i<text.length();i++){char c=text[i];if(c=='+')s+=' ';else if(c=='%'&&i+2<text.length()){auto hex=[](char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;};int a=hex(text[i+1]),b=hex(text[i+2]);if(a<0||b<0)return "";char d=a*16+b;if(!d)return "";s+=d;i+=2;}else s+=c;}return s;}
static String formField(const String& body,const char* key){unsigned start=0;while(start<body.length()){int end=body.indexOf('&',start);if(end<0)end=body.length();String f=body.substring(start,end);int eq=f.indexOf('=');if(eq>=0&&f.substring(0,eq)==key)return formDecode(f.substring(eq+1));start=end+1;}return "";}
// sdHealthy describes active logging. A clean STOPPED card is still mounted.
static bool gnssSettingsStorageReady(){return powered&&sdCardPresent()&&(sdHealthy||(stopped&&!stopRequested&&!strcmp(sdReason,"STOPPED")));}
static bool gnssWebRoute(const String& path){
 if(!path.startsWith("/api/gnss/"))return false;
 int pos=request.indexOf("\r\n\r\n");String body=pos>=0?request.substring(pos+4):String("");
 auto result=[](bool ok,const String& message){lastGnssMetaMs=0;webReply(ok?200:400,"{\"ok\":"+String(ok?"true":"false")+",\"message\":"+ntrip::jsonString(message)+"}");};
 if(path=="/api/gnss/antenna"){String name=formField(body,"antenna"),height=formField(body,"height");double h=0;bool ok=ntrip::safe(name,64)&&(height.length()==0||gnssbin::coordinate(height.c_str(),h,100));if(ok&&h>=0){ntrip::antenna=name;ntrip::antennaHeight=h;ntrip::antennaHeightKnown=height.length()>0;result(true,"アンテナ情報を記録へ反映しました");}else result(false,"アンテナ高は0〜100m、未測定なら空欄にしてください");return true;}
 if(path=="/api/gnss/config"){
  bool persist=formField(body,"save")=="1";
  // Check before replacing RAM settings or disrupting a working connection.
  if(persist&&!gnssSettingsStorageReady()){result(false,"親機SDを認識してから保存してください");return true;}
  if(persist&&(!stopped||stopRequested||runRanging||dumping)){result(false,"親機の記録・測距・転送を停止してから保存してください");return true;}
  String host=formField(body,"host"),port=formField(body,"port"),user=formField(body,"user"),password=formField(body,"password"),ssid=formField(body,"ssid"),wifiPassword=formField(body,"wifi_password");
  String mount=formField(body,"mount");
  // Blank fields retain secrets only when explicitly requested for the SAME destination.
  if(formField(body,"keep_auth")=="1"&&host==ntrip::host&&port==String(ntrip::port)&&!user.length()&&!password.length()){user=ntrip::user;password=ntrip::password;}
  if(formField(body,"keep_wifi")=="1"&&ssid==ntrip::ssid&&!wifiPassword.length())wifiPassword=ntrip::wifiPassword;
  uint16_t p=0;bool ok=ntripsettings::parsePort(port.c_str(),p)&&host.length()&&ntrip::safe(host,128)&&ntrip::safe(user,96)&&ntrip::safe(password,128)&&ntrip::safe(ssid,32)&&ntrip::safe(wifiPassword,63)&&(!mount.length()||gnssbin::mountValid(mount.c_str()));
  for(unsigned i=0;i<host.length();i++)if(!(isalnum(host[i])||host[i]=='.'||host[i]=='-'))ok=false;
  if(user.indexOf(':')>=0)ok=false;
  if(!ok){result(false,"接続先・ポート・入力長を確認してください");return true;}
  ntrip::stop();ntrip::host=host;ntrip::port=p;ntrip::user=user;ntrip::password=password;ntrip::ssid=ssid;ntrip::wifiPassword=wifiPassword;ntrip::tableReady=false;ntrip::count=ntrip::total=0;ntrip::mount=mount;
  ntrip::settingsTouched=true;ntrip::settingsMessage="変更を適用済み・SDには未保存";
  if(persist){
   if(!ntrip::saveSettings()){ntrip::settingsMessage="SD保存失敗・入力値は親機RAMに適用済み。再保存してください";result(false,ntrip::settingsMessage);return true;}
   ntrip::queueSettingsWifi();result(true,"親機SDへ保存・読戻し確認済み。記録の再開は手動で行ってください");return true;
  }
  ntrip::queueSettingsWifi();
  result(true,"設定を適用しました。再起動後も使う場合はSDへ保存してください");return true;
 }
 if(path=="/api/gnss/save"){
  if(!gnssSettingsStorageReady()){result(false,"親機SDを認識してから保存してください");return true;}
  if(!stopped||stopRequested||runRanging||dumping){result(false,"実験終了で記録と測距を停止し、転送終了後に保存してください");return true;}
  bool ok=ntrip::saveSettings();if(!ok)ntrip::settingsMessage="SD保存失敗・現在の設定は再起動で失われる場合があります";
  result(ok,ok?"接続情報と基準局を親機SDへ保存しました":ntrip::settingsMessage);return true;
 }
 if(path=="/api/gnss/load"){
  if(!gnssSettingsStorageReady()){result(false,"親機SDを認識してから読み込んでください");return true;}
  bool ok=ntrip::loadSettings();result(ok,ok?"保存設定に戻しました。補正受信は開始ボタンで再開してください":"有効な保存設定がありません。現在の設定を維持します");return true;
 }
 if(path=="/api/gnss/reconnect"){
  if(!ntrip::lastSuccessful.mount[0]||!ntripsettings::valid(ntrip::lastSuccessful)){result(false,"前回受信成功局がありません");return true;}
  if(!gnssSnapshot().f9p){result(false,"F9P受信機の機種確認ができていません");return true;}
  ntrip::applySettings(ntrip::lastSuccessful);ntrip::settingsTouched=true;ntrip::settingsMessage="前回受信成功局を適用済み（設定保存とは別）";
  ntrip::start(false);ntrip::deferStart=true;result(true,"前回受信成功局へ再接続しています");return true;
 }
 if(path=="/api/gnss/stop"){ntrip::stop();result(true,"RTK補正受信を停止しました。PPK記録は実験記録と連動します");return true;}
 if(ntrip::host.isEmpty()){result(false,"補正サーバーの接続設定を先に入力してください");return true;}
 if(path=="/api/gnss/table"||path=="/api/gnss/auto"||path=="/api/gnss/select"){
  if(!ntrip::ssid.length()){result(false,"テザリングのWi-Fi名とパスワードを入力し、接続設定を適用してください");return true;}
 }
 // Rejoin only for a validated start operation; invalid requests leave Wi-Fi alone.
 auto beginCorrection=[](bool table){if(!ntrip::online())ntrip::queueSettingsWifi();ntrip::start(table);ntrip::deferStart=true;};
 if(path=="/api/gnss/table"){ntrip::autoPending=false;beginCorrection(true);result(true,ntrip::online()?"基準局一覧の取得を受け付けました":"テザリングへ再接続してから基準局一覧を取得します。接続状態を確認してください");return true;}
 if(path=="/api/gnss/auto"){
  auto g=gnssSnapshot();if(!g.positionValid||time_us_64()-g.pvtUs>=3000000){result(false,"自動選択には親機GNSSの測位が必要です");return true;}
  ntrip::settingsTouched=true;ntrip::autoPending=true;beginCorrection(true);result(true,"一覧を取得し、100km以内の最寄り対応局を選択します。L5対応は別途確認してください");return true;
 }
 if(path=="/api/gnss/select"){
  String mount=formField(body,"mount");if(!gnssbin::mountValid(mount.c_str())){result(false,"基準局名を確認してください");return true;}
  if(!gnssSnapshot().f9p){result(false,"F9P受信機の機種確認ができていません");return true;}
  ntrip::settingsTouched=ntrip::settingsTouched||ntrip::mount!=mount;ntrip::autoPending=false;ntrip::mount=mount;beginCorrection(false);result(true,"選択した基準局へ接続しています");return true;
 }
 result(false,"未対応の操作です");return true;
}
