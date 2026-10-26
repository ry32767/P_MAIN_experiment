#include <string>
#include <map>
#include <vector>
#include <atomic>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include "parent_calibration.h"
#include "gnss_binary.h"
class String:public std::string {
public:
 using std::string::string;String(const std::string& s):std::string(s){}String(unsigned n):std::string(std::to_string(n)){}String(int n):std::string(std::to_string(n)){}
 String(float n,unsigned decimals){char b[80];snprintf(b,sizeof(b),"%.*f",decimals,double(n));assign(b);}
 bool startsWith(const char* s)const{return find(s)==0;}bool endsWith(const char* s)const{return size()>=strlen(s)&&compare(size()-strlen(s),strlen(s),s)==0;}
 bool isEmpty()const{return empty();}void toCharArray(char* out,size_t n)const{snprintf(out,n,"%s",c_str());}
 int indexOf(const char* s,size_t at=0)const{auto n=find(s,at);return n==npos?-1:int(n);}int indexOf(char c,size_t at=0)const{auto n=find(c,at);return n==npos?-1:int(n);}
 String substring(size_t at,size_t end=npos)const{return substr(at,end==npos?npos:end-at);}
};
constexpr int O_RDONLY=1,O_RDWR=2,O_CREAT=4,O_EXCL=8;
bool syncFailure=false,shortWrite=false,renameFailure=false;
std::map<std::string,std::vector<uint8_t>> files;
struct FsFile {
 std::vector<uint8_t>* data=nullptr;std::string name;bool directory=false;size_t offset=0,index=0;
 explicit operator bool()const{return data||directory;}
 bool isFile()const{return data;}
 bool getName(char* out,size_t n){snprintf(out,n,"%s",name.c_str());return name.size()<n;}
 bool openNext(FsFile* d,int){if(d->index>=files.size())return false;auto it=files.begin();std::advance(it,d->index++);data=&it->second;name=it->first;offset=0;return true;}
 size_t fileSize()const{return data->size();}
 size_t write(const void* p,size_t n){if(shortWrite)n/=2;auto b=static_cast<const uint8_t*>(p);data->assign(b,b+n);offset=n;return n;}
 int read(void* p,size_t n){n=std::min(n,data->size()-offset);memcpy(p,data->data()+offset,n);offset+=n;return n;}
 bool seekSet(size_t n){if(n>data->size())return false;offset=n;return true;}
 bool sync(){return !syncFailure;}void close(){data=nullptr;directory=false;}
};
struct {
 bool exists(const char* name){return files.count(name);}
 FsFile open(const char* name,int flags){FsFile f;if(!strcmp(name,"/")){f.directory=true;return f;}if(files.count(name)&&(flags&O_EXCL))return f;if(!files.count(name)&&!(flags&O_CREAT))return f;f.data=&files[name];f.name=name;return f;}
 bool rename(const char* a,const char* b){if(renameFailure||!files.count(a)||files.count(b))return false;files[b]=files[a];files.erase(a);return true;}
} sd;
namespace ntrip {bool safe(const String& s,unsigned n){if(s.size()>n)return false;for(auto c:s)if(uint8_t(c)<32||uint8_t(c)==127)return false;return true;}String jsonString(const String& s){return "\""+s+"\"";}}
namespace utcclock {struct Anchor {unsigned source=1;uint64_t at(uint64_t)const{return 1792886400000000ULL;}};}
utcclock::Anchor utcSnapshot(){return {};}
uint64_t time_us_64(){return 2000000;}
uint32_t sessionId=123,magRecordSequence=8;
bool stopped=true,stopRequested=false,runRanging=false,dumping=false,localControlBusy=false,storageReady=true,fresh=true;
struct Status {bool calibrationApplied=true,recording=false,commandPending=false;uint8_t calibration=0;uint32_t calibrationGeneration=7;} current;
Status spresenseSnapshot(){return current;}bool spresenseFresh(Status,uint64_t){return fresh;}
std::atomic<uint32_t> parentSetGeneration{0};
struct {bool enabled=true;} mag;auto i2c_mag_coefficients()->decltype(mag)&{return mag;}
struct {bool active=false;} calibration;auto i2c_mag_calibration()->decltype(calibration)&{return calibration;}
#include "parent_calibration_store.h"
String request,reply;int replyCode=0;
void webReply(int code,const String& body){replyCode=code;reply=body;}
bool gnssSettingsStorageReady(){return storageReady;}
// Same form decoding as gnss_web.h (included by the production web router).
String formDecode(const String& s){String out;for(unsigned i=0;i<s.size();i++){char c=s[i];if(c=='+')out+=' ';else if(c=='%'&&i+2<s.size()){char b[3]={s[i+1],s[i+2],0};out+=char(strtol(b,nullptr,16));i+=2;}else out+=c;}return out;}
String formField(const String& body,const char* key){unsigned at=0;while(at<body.size()){int end=body.indexOf('&',at);if(end<0)end=body.size();auto f=body.substring(at,end);int eq=f.indexOf('=');if(eq>=0&&f.substring(0,eq)==key)return formDecode(f.substring(eq+1));at=end+1;}return "";}
#include "parent_calibration_web.h"
#define CHECK(c) do{if(!(c)){fprintf(stderr,"Failed %d: %s\n",__LINE__,#c);return 1;}}while(0)
int main(){
 loadParentCalibration();CHECK(!parentCalibrationLoaded);
 parentcal::Record p;strcpy(p.name,"baseline");CHECK(saveParentCalibration(p));CHECK(parentCalibration.generation==1&&parentSetGeneration==1);
 auto baseline=files;CHECK(files.size()==1);
 syncFailure=true;strcpy(p.name,"sync failed");CHECK(!saveParentCalibration(p));CHECK(parentCalibration.generation==1);syncFailure=false;
 loadParentCalibration();CHECK(parentCalibration.generation==1);CHECK(files.count("P_MAIN_CALSET_00002.tmp"));
 renameFailure=true;CHECK(!saveParentCalibration(p));renameFailure=false;loadParentCalibration();CHECK(parentCalibration.generation==1);
 memset(p.name,0,sizeof(p.name));strcpy(p.name,"next");CHECK(saveParentCalibration(p));CHECK(parentCalibration.generation==4);CHECK(files["P_MAIN_CALSET_00001.bin"]==baseline["P_MAIN_CALSET_00001.bin"]);
 files["P_MAIN_CALSET_00004.bin"][50]^=1;loadParentCalibration();CHECK(parentCalibration.generation==1&&parentCalibrationRestoreError);
 auto before=files;
 auto action=[&](const String& body){request="POST / HTTP/1.1\r\n\r\n"+body;parentCalibrationWebRoute("/api/parent-calibration/save");};
 String draft="name=draft&cad=&body=&s0known=0&s1known=0&s2known=0";
 stopped=false;action(draft);CHECK(replyCode==409&&files==before);stopped=true;
 current.recording=true;action(draft);CHECK(replyCode==409&&files==before);current.recording=false;
 runRanging=true;action(draft);CHECK(replyCode==409&&files==before);runRanging=false;
 storageReady=false;action(draft);CHECK(replyCode==409&&files==before);storageReady=true;
 current.calibration=1;action(draft);CHECK(replyCode==409&&files==before);current.calibration=0;
 calibration.active=true;action(draft);CHECK(replyCode==409&&files==before);calibration.active=false;
 action("name=bad&s0known=1&s00=0&s01=0&s02=0&s03=2&s04=0&s05=0&s06=0&s1known=0&s2known=0");CHECK(replyCode==409&&files==before);
 action(draft);CHECK(replyCode==200&&parentCalibration.imuCalibration==7&&parentCalibration.magCalibration==8);
 CHECK(!parentcal::geometryComplete(parentCalibration));CHECK(parentCalibrationMatches());
 current.calibrationGeneration=9;CHECK(!parentCalibrationMatches());fresh=false;CHECK(!parentCalibrationMatches());
 action(draft);CHECK(replyCode==200&&parentCalibration.imuCalibration==0);
 fresh=true;parentcal::Record positioned=parentCalibration;
 strcpy(positioned.name,"existing");strcpy(positioned.cad,"revA");strcpy(positioned.body,"parent");
 for(unsigned i=0;i<3;i++){strcpy(positioned.sensor[i],"sensor");positioned.mounts[i].known=1;positioned.mounts[i].position[0]=float(i+1);}
 CHECK(saveParentCalibration(positioned));auto existingGeometry=parentCalibration;
 action("reference_only=1");CHECK(replyCode==200&&parentCalibration.imuCalibration==9&&parentCalibration.magCalibration==8);
 CHECK(!memcmp(parentCalibration.mounts,existingGeometry.mounts,sizeof(existingGeometry.mounts))&&!strcmp(parentCalibration.cad,"revA")&&!strcmp(parentCalibration.name,"existing"));
 stopped=false;before=files;action("reference_only=1");CHECK(replyCode==409&&files==before);stopped=true;
 files.clear();loadParentCalibration();CHECK(!parentCalibrationLoaded&&!parentSetGeneration&&parentCalibration.name[0]==0);
 action("reference_only=1");CHECK(replyCode==200&&parentCalibrationLoaded&&!parentcal::geometryComplete(parentCalibration));
 puts("PASS actual parent calibration store/API: immutable commit, failed sync/rename, CRC fallback, stop/recording/storage/calibration guards, invalid quaternion and draft/mismatch");
}
