"""Run the actual core1 calibration command handler with a fake SH2 hub. Offline only."""
from pathlib import Path
import subprocess,sys
import ziglang
r=Path(__file__).resolve().parents[1]
source=(r/'src/main.cpp').read_text(encoding='utf-8')
handler=source[source.index('bool endCalibration(){'):source.index('void imuTick(){')]
prefix=r'''
#include "bno_calibration.h"
#include "control.h"
#include <atomic>
#include <cassert>
#include <cstdio>
#include <iostream>
constexpr int SH2_OK=0,SH2_CAL_ACCEL=1,SH2_CAL_MAG=4,SH2_MAGNETIC_FIELD_CALIBRATED=3;
uint64_t fakeNow=1000000;uint64_t time_us_64(){return fakeNow;}
uint8_t mask=7;int getError=0,setError=0,saveError=0,saves=0;uint32_t reportPeriod=50000;
int sh2_getCalConfig(uint8_t* out){*out=mask;return getError;}
int sh2_setCalConfig(uint8_t value){if(setError)return setError;mask=value;return 0;}
int sh2_saveDcdNow(){saves++;return saveError;}
struct FakeBno{bool enableReport(int,uint32_t period){reportPeriod=period;return true;}}bno;
bool bnoOk=true;std::atomic<bool> calibrationActive{false};aq::BnoCalibration calibration;int calibrationError=0;
struct BnoRequest {uint32_t id;uint8_t op;};struct BnoReply {uint32_t id;aq::Control control;};
aq::Ring<BnoRequest,4>bnoRequests;aq::Ring<BnoReply,4>bnoReplies;
'''
test=r'''
aq::Control command(uint8_t op){static uint32_t id=0;uint32_t current=++id;assert(bnoRequests.push({current,op}));calibrationTick();BnoReply reply;assert(bnoReplies.pop(reply));assert(reply.id==current && reply.control.op==op);assert(reply.control.length<sizeof(reply.control.data));return reply.control;}
int main(){
 bnoOk=false;assert(command(aq::CAL_START).error==aq::IMU_ERROR);bnoOk=true;
 getError=-2;assert(command(aq::CAL_START).error==aq::IMU_ERROR);assert(!calibrationActive);getError=0;
 assert(command(aq::CAL_START).error==0);assert(calibrationActive && mask==5 && reportPeriod==20000);
 assert(command(aq::CAL_START).error==aq::BUSY);assert(command(aq::CAL_SAVE).error==aq::CAL_NOT_READY);assert(saves==0);
 for(unsigned n=0;n<=50;n++){for(unsigned i=0;i<3;i++)calibration.event(i,2,fakeNow);calibrationTick();if(n<50)fakeNow+=100000;}
 assert(calibration.ready(fakeNow));saveError=-2;assert(command(aq::CAL_SAVE).error==aq::IMU_ERROR);assert(!calibration.saved);
 saveError=0;assert(command(aq::CAL_SAVE).error==0);assert(calibration.saved && saves==2);assert(command(aq::CAL_SAVE).error==aq::BUSY);assert(saves==2);
 setError=-2;assert(command(aq::CAL_END).error==aq::IMU_ERROR);assert(calibrationActive);
 setError=0;assert(command(aq::CAL_END).error==0);assert(!calibrationActive && mask==7 && reportPeriod==50000);
 assert(command(aq::CAL_START).error==0);fakeNow+=aq::BnoCalibration::lifetimeUs;calibrationTick();assert(!calibrationActive && mask==7);
 setError=-2;assert(command(aq::CAL_START).error==aq::IMU_ERROR);assert(calibrationActive);setError=0;assert(command(aq::CAL_END).error==0);assert(!calibrationActive);
 std::cout<<"PASS actual BNO handler: absent hub, start failure, duplicate start/save, readiness gate, failed save, failed restore/retry, timeout and report rate restoration\n";
}
'''
p=r/'reports/raw/test_bno_control.cpp';p.write_text(prefix+handler+test,encoding='utf-8')
exe=r/'reports/raw/test_bno_control.exe'
zig=Path(ziglang.__file__).parent/('zig.exe' if sys.platform=='win32' else 'zig')
subprocess.run([str(zig),'c++','-std=c++17','-DIMU_HZ=100','-I'+str(r/'common'),str(p),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
