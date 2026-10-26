#include <assert.h>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include "ntrip_settings.h"
#include "control.h"
#include "gnss_binary.h"
class String:public std::string {
public:
 using std::string::string;
 String(const std::string& s):std::string(s){} String(unsigned n):std::string(std::to_string(n)){}
 String(int n):std::string(std::to_string(n)){}
 void toCharArray(char* out,size_t n)const{snprintf(out,n,"%s",c_str());}
 bool startsWith(const char* s)const{return find(s)==0;}bool isEmpty()const{return empty();}
 int indexOf(const char* s,size_t start=0)const{auto i=find(s,start);return i==npos?-1:int(i);}
 int indexOf(char c,size_t start=0)const{auto i=find(c,start);return i==npos?-1:int(i);}
 String substring(size_t start,size_t end=npos)const{return substr(start,end==npos?npos:end-start);}
};
constexpr int O_RDONLY=1,O_WRONLY=2,O_CREAT=4,O_TRUNC=8,WIFI_STA=1,WIFI_AP_STA=3,WIFI_AP=2;
uint32_t nowMs=0;uint32_t millis(){return nowMs;}
bool shortWrite=false,syncFailure=false;
struct FsFile {
 std::vector<uint8_t>* data=nullptr;
 explicit operator bool()const{return data;}
 size_t fileSize(){return data->size();}
 int read(void* out,size_t n){n=std::min(n,data->size());memcpy(out,data->data(),n);return n;}
 size_t write(const void* in,size_t n){if(shortWrite)n/=2;auto p=static_cast<const uint8_t*>(in);data->assign(p,p+n);return n;}
 bool sync(){return !syncFailure;} bool getWriteError(){return shortWrite;} void close(){}
};
struct {std::map<std::string,std::vector<uint8_t>> files;
 FsFile open(const char* path,int flags){auto it=files.find(path);if(it==files.end()&&!(flags&O_CREAT))return {};auto& v=files[path];if(flags&O_TRUNC)v.clear();return {&v};}
} sd;
struct {
 bool linkConnected=true;bool connected(){return linkConnected;}
 int modeValue=WIFI_AP;unsigned begins=0,disconnects=0;
 void mode(int value){modeValue=value;}
 void beginNoBlock(const char*,const char*){++begins;modeValue=WIFI_STA;} // Actual Arduino-Pico behavior.
 void disconnect(){++disconnects;modeValue=WIFI_AP;}
} WiFi;
namespace ntrip {
 String host="ntrip1.bizstation.jp",user,password,ssid,wifiPassword,mount="3C722D56";unsigned port=2101,count=0,total=0;bool tableReady=false;
 unsigned stoppedCount=0;void stop(){stoppedCount++;}
 bool online(){return WiFi.linkConnected;}bool deferStart=false;
 bool autoPending=false,historyPending=false;String antenna;double antennaHeight=0;bool antennaHeightKnown=false;
 bool safe(const String& s,unsigned max){if(s.length()>max)return false;for(auto c:s)if(uint8_t(c)<32||uint8_t(c)==127)return false;return true;}
 void start(bool){}
 String jsonString(const String& s){return "\""+s+"\"";}
}
#include "ntrip_settings_store.h"
String request,reply;int replyCode=0;unsigned lastGnssMetaMs=0;
bool sdHealthy=true,stopped=true,stopRequested=false,runRanging=false,dumping=false;
bool powered=true,cardPresent=true;const char* sdReason="NONE";bool sdCardPresent(){return cardPresent;}
void webReply(int code,const String& s){replyCode=code;reply=s;}
struct {bool f9p=true,positionValid=true;uint64_t pvtUs=0;} snapshot;
auto gnssSnapshot(){return snapshot;}uint64_t time_us_64(){return 1;}
#include "gnss_web.h"
void action(const char* path,const char* body=""){request=String("POST / HTTP/1.0\r\n\r\n")+body;assert(gnssWebRoute(path));}
int main(){
 using namespace ntrip;
 ssid="test-hotspot";connectSettingsWifi();assert(WiFi.modeValue==WIFI_AP_STA&&WiFi.begins==1);
 connectSettingsWifi();assert(WiFi.begins==1); // No reconnect for unchanged credentials.
 wifiPassword="new-secret";queueSettingsWifi();assert(wifiSettingsPending&&WiFi.begins==1);
 connectSettingsWifi();assert(!wifiSettingsPending&&WiFi.begins==2);
 WiFi.linkConnected=false;connectSettingsWifi();assert(WiFi.begins==3);WiFi.linkConnected=true;
 ssid="";connectSettingsWifi();assert(WiFi.modeValue==WIFI_AP&&WiFi.disconnects==1);
 assert(ntripsettings::crc("123456789",9)==0xcbf43926);
 assert(!loadSettings());assert(host=="ntrip1.bizstation.jp");
 user="private-user";password="private-ntrip-pass";ssid="hotspot";wifiPassword="private-wifi-pass";
 assert(saveSettings());assert(sd.files.size()==1);auto original=sd.files[settingsPaths[0]];
 host="changed.example";mount="other";assert(saveSettings());assert(sd.files.size()==2);
 host="in-memory";assert(loadSettings());assert(host=="changed.example"&&password=="private-ntrip-pass"&&wifiPassword=="private-wifi-pass");
 // A partial next save cannot destroy the last complete slot.
 shortWrite=true;host="failed.example";assert(!saveSettings());shortWrite=false;
 assert(loadSettings());assert(host=="changed.example");
 syncFailure=true;host="sync-failed.example";assert(!saveSettings());syncFailure=false;
 // A failed sync may or may not have reached the card. Corrupt it to model power loss.
 sd.files[settingsPaths[0]].resize(20);assert(loadSettings());assert(host=="changed.example");
 sd.files[settingsPaths[1]][30]^=1;host="keep-current.example";assert(!loadSettings());assert(host=="keep-current.example");
 sd.files[settingsPaths[0]]=original;assert(loadSettings());assert(host=="ntrip1.bizstation.jp");
 // Boot/recovery must not replace a user's newer in-memory settings.
 settingsBootChecked=false;settingsTouched=true;host="field.example";bootSettings();assert(host=="field.example");
 settingsBootChecked=false;settingsTouched=false;bootSettings();assert(host=="ntrip1.bizstation.jp");host="live.example";bootSettings();assert(host=="live.example");
 auto json=settingsJson();assert(json.find("private-user")==std::string::npos&&json.find("private-ntrip-pass")==std::string::npos&&json.find("private-wifi-pass")==std::string::npos);
 assert(!aq::safeFilename("AQ_NTRIP_A.cfg"));assert(!aq::safeFilename("aq_ntrip_b.CfG"));assert(aq::safeFilename("P_MAIN_GNSS_00001.ubx"));
 ntripsettings::Record a{},b{};memcpy(&a,original.data(),sizeof(a));assert(ntripsettings::valid(a));
 b=a;b.generation=0xffffffff;ntripsettings::seal(b);a.generation=0;ntripsettings::seal(a);assert(ntripsettings::newest(b,true,a,true)==1);
 assert(ntripsettings::newest(a,false,b,false)==-1);
 memset(a.host,'a',sizeof(a.host));ntripsettings::seal(a);assert(!ntripsettings::valid(a));
 b.user[0]=':';ntripsettings::seal(b);assert(!ntripsettings::valid(b));
 uint16_t parsed=0;assert(ntripsettings::parsePort("65535",parsed)&&parsed==65535);
 for(auto s:{"","0","65536","4294969397","-1","2101bad"})assert(!ntripsettings::parsePort(s,parsed));
 // Exercise the actual API handlers, including credential destination boundaries.
 host="ntrip1.bizstation.jp";port=2101;ssid="hotspot";user="private-user";password="private-ntrip-pass";wifiPassword="private-wifi-pass";
 action("/api/gnss/config","host=ntrip1.bizstation.jp&port=2101&ssid=hotspot&mount=station&keep_auth=1&keep_wifi=1");
 assert(replyCode==200&&user=="private-user"&&password=="private-ntrip-pass"&&wifiPassword=="private-wifi-pass");
 action("/api/gnss/config","host=other.example&port=2101&ssid=other-hotspot&mount=station&keep_auth=1&keep_wifi=1");
 assert(replyCode==200&&user.empty()&&password.empty()&&wifiPassword.empty());
 user="remove-me";password="remove-me-too";
 action("/api/gnss/config","host=other.example&port=2101&ssid=other-hotspot&mount=station&keep_auth=0");assert(replyCode==200&&user.empty()&&password.empty());
 action("/api/gnss/config","host=bad.example&port=4294969397&mount=station");assert(replyCode==400&&host=="other.example");
 stopped=false;auto filesBefore=sd.files;action("/api/gnss/save");assert(replyCode==400&&sd.files==filesBefore);
 stopped=true;runRanging=true;action("/api/gnss/save");assert(replyCode==400&&sd.files==filesBefore);
 runRanging=false;dumping=true;action("/api/gnss/save");assert(replyCode==400&&sd.files==filesBefore);
 dumping=false;sdHealthy=false;action("/api/gnss/save");assert(replyCode==400&&sd.files==filesBefore);
 // finishStop() sets sdHealthy=false, but leaves a clean mounted card at STOPPED.
 sdReason="STOPPED";action("/api/gnss/save");assert(replyCode==200);
 cardPresent=false;action("/api/gnss/save");assert(replyCode==400);cardPresent=true;
 host="unsaved.example";action("/api/gnss/load");assert(replyCode==200&&host=="other.example");
 // A single POST persists before scheduling the potentially disruptive join.
 wifiSettingsPending=false;auto beginsBefore=WiFi.begins;
 action("/api/gnss/config","host=atomic.example&port=2101&ssid=new-hotspot&mount=station&save=1");
 assert(replyCode==200&&settingsSaved&&!settingsTouched&&wifiSettingsPending&&WiFi.begins==beginsBefore);
 ntripsettings::Record persisted[2]{};int slot=readSettingsPair(persisted);
 assert(slot>=0&&!strcmp(persisted[slot].host,"atomic.example")&&!strcmp(persisted[slot].ssid,"new-hotspot"));
 stopped=false;action("/api/gnss/config","host=blocked.example&port=2101&save=1");
 assert(replyCode==400&&host=="atomic.example");stopped=true;
 wifiSettingsPending=false;shortWrite=true;
 action("/api/gnss/config","host=failed.example&port=2101&save=1");
 assert(replyCode==400&&settingsTouched&&!wifiSettingsPending);shortWrite=false;
 assert(loadSettings()&&host=="atomic.example");
 // Re-enable a hotspot after a failed join without changing its credentials.
 ssid="hotspot";WiFi.linkConnected=false;wifiSettingsPending=false;deferStart=false;
 action("/api/gnss/table");assert(replyCode==200&&wifiSettingsPending&&deferStart);
 auto oldBegins=WiFi.begins;connectSettingsWifi();assert(WiFi.begins==oldBegins+1);
 WiFi.linkConnected=true;wifiSettingsPending=false;action("/api/gnss/table");assert(replyCode==200&&!wifiSettingsPending);
 ssid="";action("/api/gnss/table");assert(replyCode==400&&!wifiSettingsPending);
 // Background retries wait 30s and work across millis rollover.
 ssid="retry-hotspot";WiFi.linkConnected=false;nowMs=0xfffffff0u;connectSettingsWifi();
 oldBegins=WiFi.begins;nowMs+=29999;wifiSettingsTick();assert(WiFi.begins==oldBegins);
 nowMs+=1;wifiSettingsTick();assert(WiFi.begins==oldBegins+1);
 WiFi.linkConnected=true;nowMs+=30000;wifiSettingsTick();assert(WiFi.begins==oldBegins+1);
 // Only a CRC-validated stream invokes rememberSuccessfulStream(). Attempts
 // and settings saves alone must never become successful history.
 assert(!lastSuccessful.mount[0]);action("/api/gnss/reconnect");assert(replyCode==400);
 host="success.example";port=2101;mount="BASE_OK";ssid="field-hotspot";user="history-user";password="history-secret";
 rememberSuccessfulStream();assert(historyPending&&!historySaved);auto beforeHistory=sd.files;
 historyTick(false);assert(sd.files==beforeHistory);
 historyTick(true);assert(historySaved&&!historyPending&&sd.files.count(historyPaths[0]));
 beforeHistory=sd.files;rememberSuccessfulStream();historyTick(true);assert(sd.files==beforeHistory); // No repeated writes.
 host="failed-attempt.example";mount="FAILED";
 snapshot.f9p=false;action("/api/gnss/reconnect");assert(replyCode==400&&host=="failed-attempt.example");
 snapshot.f9p=true;action("/api/gnss/reconnect");assert(replyCode==200&&host=="success.example"&&mount=="BASE_OK"&&user=="history-user"&&password=="history-secret"&&deferStart);
 mount="BASE_NEW";rememberSuccessfulStream();historyChecked=false;loadHistory();assert(!strcmp(lastSuccessful.mount,"BASE_NEW")); // Pending success beats older disk history.
 nowMs=0xfffffff0u;shortWrite=true;historyTick(true);assert(historyPending&&!historySaved);shortWrite=false;
 beforeHistory=sd.files;nowMs+=29999;historyTick(true);assert(sd.files==beforeHistory);
 nowMs+=1;historyTick(true);assert(historySaved&&!historyPending);
 // Reboot loads the last good slot without applying it over current config.
 historyChecked=false;historySaved=false;memset(&lastSuccessful,0,sizeof(lastSuccessful));host="edited.example";loadHistory();assert(historySaved&&host=="edited.example"&&!strcmp(lastSuccessful.mount,"BASE_NEW"));
 sd.files[historyPaths[1]].resize(10);historyChecked=false;loadHistory();assert(!strcmp(lastSuccessful.mount,"BASE_OK"));
 json=settingsJson();assert(json.find("history-user")==std::string::npos&&json.find("history-secret")==std::string::npos);
 assert(!aq::safeFilename("AQ_LAST_A.cfg")&&!aq::safeFilename("AQ_LAST_B.cfg"));
 puts("PASS successful station history: independent slots, guarded writes, no repeat writes, retry, reboot, corruption fallback, reconnect and secret redaction");
 puts("PASS NTRIP SD round-trip, dual-slot recovery, truncation/CRC, sync failure, boot guard, secret redaction/download denial and generation wrap");
 puts("PASS actual GNSS API: same-destination secret retention, destination-change clearing, explicit clear, strict port bounds and recording/ranging/transfer/SD save guards");
}
