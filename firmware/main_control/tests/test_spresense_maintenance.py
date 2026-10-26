"""Compile the real guarded USB maintenance function against a fake SD."""
from pathlib import Path
import subprocess,sys,ziglang
r=Path(__file__).resolve().parents[1]
source=(r/'firmware/spresense/logger/logger.ino').read_text(encoding='utf-8').split('bool calibrationMaintenance(')[1].split('bool startTask')[0]
stub=r'''
#include <string>
#include <vector>
#include <map>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cerrno>
#include "spresense_calibration.h"
std::map<std::string,std::vector<uint8_t>> files;std::map<int,std::pair<std::string,size_t>> handles;int nextFd=1;
constexpr int O_RDONLY=0;
struct FakeStat{};
int fakeOpen(const char* p,int){if(!files.count(p))return -1;handles[++nextFd]={p,0};return nextFd;}
int fakeRead(int fd,void* out,size_t n){auto& h=handles.at(fd);auto& b=files.at(h.first);n=std::min(n,b.size()-h.second);memcpy(out,b.data()+h.second,n);h.second+=n;return n;}
int fakeClose(int fd){handles.erase(fd);return 0;}
int fakeUnlink(const char* p){return files.erase(p)?0:-1;}
int fakeStat(const char* p,FakeStat*){if(files.count(p))return 0;errno=ENOENT;return -1;}
#define open fakeOpen
#define read fakeRead
#define close fakeClose
#define unlink fakeUnlink
#define stat fakeStat
#define pthread_mutex_lock(x) ((void)0)
#define pthread_mutex_unlock(x) ((void)0)
int calibrationMutex;
std::atomic<bool> operationBusy{false};bool recording=false,safelyStopped=true;
std::atomic<uint8_t> calibrationSaveError{0};sprcal::Session calibrationSession;sprcal::Coefficients appliedCalibration;
struct {template<class... T>void printf(const char*,T...){}void println(const char*){}} Serial;
void put(const char* p,const sprcal::Coefficients& c){uint8_t bytes[64];sprcal::encode(c,bytes);files[p]=std::vector<uint8_t>(bytes,bytes+64);}
bool persistCalibration(sprcal::Coefficients& c){c.generation=appliedCalibration.generation+1;put("/mnt/sd0/K00003.CAL",c);return true;}
'''
# FakeStat is a type and fakeStat a function; replace only the C struct spelling.
source=('bool calibrationMaintenance('+source).replace('struct stat info','FakeStat info')
checks=r'''
int main(){
 auto& c=appliedCalibration;c.generation=2;c.enabled=false;c.accelBias[0]=.016f;
 put("/mnt/sd0/K00002.CAL",c);auto original=files.at("/mnt/sd0/K00002.CAL");
 auto one=c;one.generation=1;one.enabled=true;put("/mnt/sd0/K00001.CAL",one);
 recording=true;assert(!calibrationMaintenance("CAL ENABLE_SAVED"));recording=false;
 assert(calibrationMaintenance("CAL ENABLE_SAVED")&&c.enabled&&c.generation==3&&files.size()==3);
 assert(files.at("/mnt/sd0/K00002.CAL")==original);
 assert(!calibrationMaintenance("CAL DELETE_DISABLED K00001.CAL 00000000"));
 assert(!calibrationMaintenance("CAL DELETE_DISABLED K00002.CAL 00000000"));
 assert(!calibrationMaintenance("CAL DELETE_DISABLED ../K00002.CAL 12345678"));
 char command[100];snprintf(command,sizeof(command),"CAL DELETE_DISABLED K00002.CAL %08x",unsigned(spnav::get32(original.data()+60)));
 assert(calibrationMaintenance(command)&&files.size()==2&&!files.count("/mnt/sd0/K00002.CAL"));
 assert(files.count("/mnt/sd0/K00001.CAL")&&files.count("/mnt/sd0/K00003.CAL"));
 operationBusy=true;assert(!calibrationMaintenance("CAL ENABLE_SAVED")&&operationBusy);
 puts("PASS actual calibration maintenance: stopped only, enable preserves coefficients, matching disabled file only, enabled/path/CRC/busy refusal");
}
'''
path=r/'reports/raw/test_spresense_maintenance.cpp';path.write_text(stub+source+checks,encoding='utf-8')
target=path.with_suffix('.exe');zig=Path(ziglang.__file__).parent/('zig.exe' if sys.platform=='win32' else 'zig')
subprocess.run([str(zig),'c++','-std=c++17','-I'+str(r/'common'),str(path),'-o',str(target)],check=True)
subprocess.run([str(target)],check=True)
