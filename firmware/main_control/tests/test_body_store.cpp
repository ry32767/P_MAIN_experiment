#include <string>
#include <map>
#include <vector>
#include <atomic>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include "body_calibration.h"
#include "control.h"
#include "binary_log.h"
#include "gnss_binary.h"
class String:public std::string {
public:
 using std::string::string;String(const std::string& s):std::string(s){}String(unsigned n):std::string(std::to_string(n)){}String(int n):std::string(std::to_string(n)){}
 String(float n,unsigned decimals){char b[80];snprintf(b,sizeof(b),"%.*f",decimals,double(n));assign(b);}
 bool startsWith(const std::string& s)const{return find(s)==0;}
 bool startsWith(const char* s)const{return find(s)==0;}bool endsWith(const char* s)const{return size()>=strlen(s)&&compare(size()-strlen(s),strlen(s),s)==0;}
 bool isEmpty()const{return empty();}void toCharArray(char* out,size_t n)const{snprintf(out,n,"%s",c_str());}
 int indexOf(const char* s,size_t at=0)const{auto n=find(s,at);return n==npos?-1:int(n);}int indexOf(char c,size_t at=0)const{auto n=find(c,at);return n==npos?-1:int(n);}
 String substring(size_t at,size_t end=npos)const{return substr(at,end==npos?npos:end-at);}
};
constexpr int O_RDONLY=1,O_RDWR=2,O_WRONLY=2,O_CREAT=4,O_EXCL=8;
bool syncFailure=false,shortWrite=false,renameFailure=false;
std::map<std::string,std::vector<uint8_t>> files;
struct FsFile {
 std::vector<uint8_t>* data=nullptr;std::string name;bool directory=false;size_t offset=0,index=0;
 explicit operator bool()const{return data||directory;}
 bool isFile()const{return data;}
 bool getName(char* out,size_t n){snprintf(out,n,"%s",name.c_str());return name.size()<n;}
 bool openNext(FsFile* d,int){if(d->index>=files.size())return false;auto it=files.begin();std::advance(it,d->index++);data=&it->second;name=it->first;offset=0;return true;}
 size_t fileSize()const{return data->size();}
 size_t write(const void* p,size_t n){if(shortWrite)n/=2;auto b=static_cast<const uint8_t*>(p);if(offset+n>data->size())data->resize(offset+n);memcpy(data->data()+offset,b,n);offset+=n;return n;}
 int read(void* p,size_t n){n=std::min(n,data->size()-offset);memcpy(p,data->data()+offset,n);offset+=n;return n;}
 size_t position()const{return offset;}
 bool seekSet(size_t n){if(n>data->size())return false;offset=n;return true;}
 bool sync(){return !syncFailure;}void close(){data=nullptr;directory=false;}
};
struct {
 bool exists(const char* name){return files.count(name);}
 FsFile open(const char* name,int flags){FsFile f;if(!strcmp(name,"/")){f.directory=true;return f;}if(files.count(name)&&(flags&O_EXCL))return f;if(!files.count(name)&&!(flags&O_CREAT))return f;f.data=&files[name];f.name=name;return f;}
 bool rename(const char* a,const char* b){if(renameFailure||!files.count(a)||files.count(b))return false;files[b]=files[a];files.erase(a);return true;}
} sd;

#include <cassert>
namespace utcclock {struct Anchor {unsigned source=1;uint64_t at(uint64_t n)const{return 1792886400000000ULL+n;}};}
utcclock::Anchor utcSnapshot(){return {};}
uint64_t fakeNow=1000000;uint64_t time_us_64(){return fakeNow;}
const char* role="C_MAIN";uint32_t sessionId=123;
bool stopped=false,stopRequested=false,runRanging=false,dumping=false,calibrationActive=false,sdHealthy=true,powered=true;
const char* sdReason="NONE";FsFile slowFile;char logName[64]="imu.bin",rawName[64]="raw.bin",slowName[64]="health.bin";
bool sdCardPresent(){return true;}
struct {const char* getChipID(){return "synthetic";}}rp2040;
aq::Health health(){aq::Health h;h.flags=aq::IMU;h.imuAgeUs=10;return h;}
bool writeBinary(FsFile& f,uint16_t kind,uint64_t t,uint32_t seq,const bodycal::Event& e){
 aqbin::RecordHeader h;h.kind=kind;h.bytes=sizeof(e);h.monoUs=t;h.sequence=seq;aqbin::seal(h,&e);
 return f.write(&h,sizeof(h))==sizeof(h)&&f.write(&e,sizeof(e))==sizeof(e);
}
template<class T> bool writeBinary(FsFile& f,uint16_t kind,uint64_t t,uint32_t seq,const T& p){return false;}
#include "body_calibration_store.h"
int main(){
 loadBodyCalibration();assert(!bodyLoaded&&!bodyRestoreError&&!bodyExperimentReady());bodyDcdGeneration=1;
 assert(bodyBegin());assert(bodyCapture.id==1);auto name=std::string(bodyCaptureName);
 for(unsigned phase=1;phase<=5;phase++){
  assert(bodyCapture.mark(phase,false,fakeNow));assert(bodyEvent(2,phase));fakeNow+=5000000;
  assert(bodyCapture.mark(phase,true,fakeNow));assert(bodyEvent(3,phase));fakeNow+=100000;
 }
 assert(bodyCapture.complete());assert(bodyEvent(4,0));bodyCaptureFile.close();bodyCapture.active=false;bodyRecordingActive=false;
 auto original=files[name];bodycal::Solution s;s.capture=1;s.boot=sessionId;s.imu=s.mag=1;s.axes=3;
 assert(bodyCompleted(s));stopped=true;sdHealthy=false;sdReason="STOPPED";assert(bodyImport(s));assert(bodyLoaded&&bodyExperimentReady());
 assert(bodyProfile.generation==1&&files[name]==original);auto snapshot=files;
 s.boot++;assert(!bodyImport(s));s.boot--;s.imu++;assert(!bodyImport(s));s.imu--;
 s.imuQ[0]=2;assert(!bodyImport(s));s.imuQ[0]=1;
 syncFailure=true;assert(!bodyImport(s));assert(bodyProfile.generation==1);syncFailure=false;
 assert(files.count("C_MAIN_BODY_ALIGN_00002.tmp"));assert(bodyImport(s));assert(bodyProfile.generation==3);
 assert(files["C_MAIN_BODY_ALIGN_00001.bin"]==snapshot["C_MAIN_BODY_ALIGN_00001.bin"]);
 loadBodyCalibration();assert(bodyLoaded&&bodyProfile.generation==3&&bodyLastCapture==1);bodyDcdGeneration=1;assert(bodyExperimentReady());
 stopped=false;sdHealthy=true;sdReason="NONE";assert(bodyBegin(true)&&bodyLastAxesCapture==1&&bodyCapture.required==16);
 assert(!bodyCapture.mark(1,false,fakeNow));assert(bodyCapture.mark(5,false,fakeNow));bodyMotion.begin(fakeNow);assert(bodyEvent(2,5));
 fakeNow+=4000000;bodyMotion.observe(1,fakeNow,1);fakeNow+=2000000;bodyMotion.observe(2,fakeNow,2);assert(!bodyMotion.ready(fakeNow));
 fakeNow+=24000000;bodyMotion.observe(0,fakeNow,3);assert(bodyMotion.ready(fakeNow));assert(bodyCapture.mark(5,true,fakeNow));assert(bodyEvent(3,5));bodyMotionAccepted=true;
 assert(bodyFinish()&&bodyCameraReady()&&bodyExperimentReady());assert(bodyLastCapture==2&&bodyLastAxesCapture==1);
 assert(!bodyCompleted(bodycal::Solution{2,sessionId,1,1}));
 loadBodyCalibration();bodyDcdGeneration=1;assert(bodyExperimentReady()&&!bodyCameraReady()&&bodyLastAxesCapture==1&&bodyLastCapture==2);
 bodyDcdGeneration=2;assert(!bodyExperimentReady());bodyDcdGeneration=1;
 files["C_MAIN_BODY_ALIGN_00003.bin"][10]^=1;loadBodyCalibration();assert(bodyLoaded&&bodyProfile.generation==1&&bodyRestoreError);assert(!bodyExperimentReady());
 assert(files[name]==original);files[name].back()^=1;assert(!bodyCompleted(s));files[name]=original;
 stopped=false;sdHealthy=true;sdReason="NONE";bodyDcdGeneration=1;assert(bodyBegin());assert(!bodyExperimentReady());bodyAbort();assert(!bodyActive()&&!bodyRecordingActive);
 s.capture=2;assert(!bodyCompleted(s));
 stopped=true;sdHealthy=false;sdReason="STOPPED";assert(bodySaveDcdReceipt());assert(bodyDcdGeneration==2);loadBodyCalibration();assert(bodyDcdGeneration==2);
 bodyProfile.camera=1;bodyCameraReadyAtomic=false;assert(!bodyCameraReady());bodyProfile.generation=99999;bodyLastCapture=99999;bodyDcdGeneration=99999;bodyMotion.began=1;fakeNow=1000000000;assert(bodyJson().size()<=sizeof(aq::Control{}.data));
 puts("PASS actual body SD store: immutable capture/profile, camera-only reuse, mandatory fresh movement, restart invalidation, CRC fallback, receipt and compact status");
}
