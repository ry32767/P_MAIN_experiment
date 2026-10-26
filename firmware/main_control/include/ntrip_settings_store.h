#pragma once
#include "ntrip_settings.h"
namespace ntrip {
bool settingsTouched=false,settingsBootChecked=false,settingsSaved=false;
String settingsMessage="SDの読込み待ち";
const char* settingsPaths[2]={"AQ_NTRIP_A.cfg","AQ_NTRIP_B.cfg"};
bool wifiSettingsPending=false,wifiSettingsApplied=false;
uint32_t wifiLastAttempt=0,wifiJoinAttempts=0;
String appliedSsid,appliedWifiPassword;
void queueSettingsWifi(){wifiSettingsPending=true;}
bool readSettings(unsigned slot,ntripsettings::Record& r,const char* const* paths=settingsPaths){
 FsFile f=sd.open(paths[slot],O_RDONLY);if(!f)return false;
 bool ok=f.fileSize()==sizeof(r)&&f.read(&r,sizeof(r))==sizeof(r);f.close();return ok&&ntripsettings::valid(r);
}
int readSettingsPair(ntripsettings::Record (&r)[2],const char* const* paths=settingsPaths){bool a=readSettings(0,r[0],paths),b=readSettings(1,r[1],paths);return ntripsettings::newest(r[0],a,r[1],b);}
void connectSettingsWifi(){
 wifiSettingsPending=false;
 // NTRIP-only edits must not tear down the browser's working Wi-Fi link.
 if(wifiSettingsApplied&&ssid==appliedSsid&&wifiPassword==appliedWifiPassword&&(!ssid.length()||WiFi.connected()))return;
 appliedSsid=ssid;appliedWifiPassword=wifiPassword;wifiSettingsApplied=true;
 if(ssid.length()){
  wifiLastAttempt=millis();++wifiJoinAttempts;
  WiFi.mode(WIFI_AP_STA);WiFi.beginNoBlock(ssid.c_str(),wifiPassword.c_str());
  // Arduino-Pico _beginInternal preserves the AP hardware but sets _mode=STA.
  // Restore the combined mode so routing/status and NTRIP retain both roles.
  WiFi.mode(WIFI_AP_STA);
 }else {WiFi.mode(WIFI_AP_STA);WiFi.disconnect();WiFi.mode(WIFI_AP);}
}
void wifiSettingsTick(){
 // Called only between HTTP clients, so rejoining cannot truncate a reply.
 if(wifiSettingsPending){connectSettingsWifi();return;}
 if(wifiSettingsApplied&&ssid.length()&&!WiFi.connected()&&uint32_t(millis()-wifiLastAttempt)>=30000)connectSettingsWifi();
}
void applySettings(const ntripsettings::Record& r){
 stop();host=r.host;port=r.port;user=r.user;password=r.password;ssid=r.ssid;wifiPassword=r.wifiPassword;mount=r.mount;
 tableReady=false;count=total=0;queueSettingsWifi();
}
bool loadSettings(){
 ntripsettings::Record records[2]{};int slot=readSettingsPair(records);
 if(slot<0){settingsSaved=false;settingsMessage="有効な保存設定なし（現在の設定を維持）";return false;}
 applySettings(records[slot]);settingsSaved=true;settingsTouched=false;settingsMessage="SDの保存設定を読込み済み（補正開始待ち）";return true;
}
void bootSettings(){
 // Recovery must never overwrite settings entered after boot.
 if(settingsBootChecked)return;settingsBootChecked=true;
 if(!settingsTouched)loadSettings();
}
bool saveSettingsRecord(ntripsettings::Record r,const char* const* paths){
 ntripsettings::Record records[2]{};int active=readSettingsPair(records,paths);unsigned target=active==0?1:0;
 r.generation=active<0?1:records[active].generation+1;ntripsettings::seal(r);
 if(!ntripsettings::valid(r))return false;
 // Only the inactive SETTINGS slot is replaced. Log files are never touched.
 FsFile f=sd.open(paths[target],O_WRONLY|O_CREAT|O_TRUNC);if(!f)return false;
 bool ok=f.write(&r,sizeof(r))==sizeof(r)&&f.sync()&&!f.getWriteError();f.close();
 ntripsettings::Record verify{};ok=ok&&readSettings(target,verify,paths)&&!memcmp(&r,&verify,sizeof(r));
 return ok;
}
ntripsettings::Record currentSettings(){
 ntripsettings::Record r{};r.port=port;
 host.toCharArray(r.host,sizeof(r.host));user.toCharArray(r.user,sizeof(r.user));password.toCharArray(r.password,sizeof(r.password));
 ssid.toCharArray(r.ssid,sizeof(r.ssid));wifiPassword.toCharArray(r.wifiPassword,sizeof(r.wifiPassword));mount.toCharArray(r.mount,sizeof(r.mount));ntripsettings::seal(r);return r;
}
bool saveSettings(){bool ok=saveSettingsRecord(currentSettings(),settingsPaths);if(ok){settingsSaved=true;settingsTouched=false;settingsMessage="SDへ保存・読戻し確認済み";}return ok;}
const char* historyPaths[2]={"AQ_LAST_A.cfg","AQ_LAST_B.cfg"};
ntripsettings::Record lastSuccessful{};bool historySaved=false,historyChecked=false;uint32_t historyAttemptAt=0;bool historyAttempted=false;
String historyMessage="前回受信局のSD読込み待ち";
void loadHistory(){
 if(historyChecked)return;historyChecked=true;ntripsettings::Record records[2]{};int slot=readSettingsPair(records,historyPaths);
 if(slot>=0&&!historyPending){lastSuccessful=records[slot];historySaved=true;historyMessage="前回受信成功局をSDから読込み済み";}
 else if(!historyPending)historyMessage="保存された前回受信成功局なし";
}
void rememberSuccessfulStream(){
 auto next=currentSettings();
 // Generation and checksum differ across writes; compare only the payload.
 if(historySaved&&!memcmp(&next.port,&lastSuccessful.port,offsetof(ntripsettings::Record,checksum)-offsetof(ntripsettings::Record,port)))return;
 lastSuccessful=next;historyPending=true;historySaved=false;historyAttempted=false;historyMessage="受信成功・前回局のSD保存待ち";
}
void historyTick(bool storageReady){
 if(!storageReady)return;loadHistory();if(!historyPending)return;
 if(historyAttempted&&uint32_t(millis()-historyAttemptAt)<30000)return;
 historyAttempted=true;historyAttemptAt=millis();
 if(saveSettingsRecord(lastSuccessful,historyPaths)){historyPending=false;historySaved=true;historyMessage="前回受信成功局をSDへ保存・読戻し確認済み";}
 else historyMessage="前回局のSD保存失敗・30秒後に再試行";
}
String settingsJson(){
 // Credentials are write-only; this endpoint is separate from measurement logs.
 return "{\"last_available\":"+String(lastSuccessful.mount[0]?"true":"false")+",\"last_saved\":"+String(historySaved?"true":"false")+",\"last_host\":"+jsonString(lastSuccessful.host)+",\"last_port\":"+String(lastSuccessful.port)+",\"last_mount\":"+jsonString(lastSuccessful.mount)+",\"last_message\":"+jsonString(historyMessage)+",\"host\":"+jsonString(host)+",\"port\":"+String(port)+",\"mount\":"+jsonString(mount)+",\"ssid\":"+jsonString(ssid)+",\"has_auth\":"+String(user.length()?"true":"false")+",\"has_wifi_password\":"+String(wifiPassword.length()?"true":"false")+",\"saved\":"+String(settingsSaved?"true":"false")+",\"dirty\":"+String(settingsTouched?"true":"false")+",\"boot_checked\":"+String(settingsBootChecked?"true":"false")+",\"message\":"+jsonString(settingsMessage)+"}";
}
}
