"""Compile the production start guard and verify rejection before filesystem side effects."""
from pathlib import Path
import subprocess,sys,ziglang
r=Path(__file__).resolve().parents[1]
source=(r/'include/experiment_control.h').read_text(encoding='utf-8').split('static void executeControl()')[0]
clock=(r/'include/body_calibration_store.h').read_text(encoding='utf-8').split('bool bodyClockReady(){')[1].split('bool bodyExperimentReady()')[0]
clock='bool bodyClockReady(){'+clock.replace('#ifdef BOARD_PARENT','#if 1')
camera_guard=next(line for line in (r/'include/web_control.h').read_text(encoding='utf-8').splitlines() if line.strip().startswith('if(!bodyCameraReady()||!remoteCamera)'))
camera_guard='void cameraRange(bool remoteCamera){'+camera_guard+' rangeStarted=true;}\n'
main=(r/'src/main.cpp').read_text(encoding='utf-8')
schedule='if(p.kind==aq::SCHEDULE'+main.split('}else if(p.kind==aq::SCHEDULE')[1].split('}else if(p.kind==aq::STOP')[0]
sync=schedule.split('aq::AcquisitionConfig config')[0].replace('clock.','childClock.')
arm=next(line.strip() for line in schedule.splitlines() if line.strip().startswith('if(p.t2 && validPattern'))
schedule_test='void childSchedule(const aq::Packet& p,bool validPattern){'+sync+arm+'armed=true;}}}\n'
stub=r'''
#include <cassert>
#include <cstdio>
#include "control.h"
#include "body_calibration.h"
bool dumping=false,ready=false,active=false,monitorWeb=true;
unsigned stops=0,monitorStops=0;
unsigned millis(){return 123;}
bool bodyExperimentReady(){return ready;}bool bodyActive(){return active;}
void monitorStop(){monitorStops++;}void stopLog(){stops++;}
bool clockFresh=true,clockValid=true;
struct Spr {bool recording=true,imuTimeReady=true,ppsLocked=true,synchronized=true;unsigned timeSource=1;} spr;
Spr spresenseSnapshot(){return spr;}bool spresenseFresh(const Spr&,uint64_t){return clockFresh;}
struct Utc {unsigned source=1;uint64_t at(uint64_t)const{return clockValid?1792886400000000ULL:0;}} utc;
Utc utcSnapshot(){return utc;}uint64_t time_us_64(){return 123456;}
bool cameraProof=false,rangeStarted=false;int webStatus=0;
bool bodyCameraReady(){return cameraProof;}void webReply(int code,const char*){webStatus=code;}
bool calibrationActive=false,bodyRecordingActive=false,bodyCameraReadyAtomic=false,armed=false,syncOk=false;
uint32_t peerSession=77;aq::Packet pendingReply;uint64_t scheduled=0,armDeadline=0;int stateMutex;
int64_t syncOffset=0;uint32_t syncUncertainty=0;
struct {int64_t offset=0;uint32_t uncertainty=0;bool ok=false;}childClock;
void armMarker(bool){}
#define mutex_enter_blocking(x) ((void)0)
#define mutex_exit(x) ((void)0)
'''
checks=r'''
int main(){
 aq::Control c;c.op=aq::RECORD_NEW;
 assert(beginControl(c)&&localAnswer.error==aq::CAL_WARNING&&!localControlBusy&&stops==0&&monitorStops==0);
 c.arg=bodycal::acknowledgeUncalibrated;assert(beginControl(c)&&stops==1&&monitorStops==1);localControlBusy=false;
 active=true;assert(beginControl(c)&&localAnswer.error==aq::BUSY&&stops==1);active=false;
 c.op=aq::RECORD_START;c.arg=0;ready=true;assert(beginControl(c)&&localControlBusy&&stops==1);localControlBusy=false;
 ready=false;assert(beginControl(c)&&localAnswer.error==aq::CAL_WARNING&&stops==1);
 c.op=aq::RECORD_STOP;assert(beginControl(c)&&stops==2);
 assert(bodyClockReady());utc.source=3;assert(!bodyClockReady());utc.source=1;spr.timeSource=3;assert(!bodyClockReady());spr.timeSource=1;
 clockFresh=false;assert(!bodyClockReady());clockFresh=true;clockValid=false;assert(!bodyClockReady());clockValid=true;
 spr.recording=false;assert(!bodyClockReady());spr.recording=true;spr.ppsLocked=false;assert(!bodyClockReady());spr.ppsLocked=true;
 spr.synchronized=false;assert(!bodyClockReady());spr.synchronized=true;assert(bodyClockReady());
 cameraRange(true);assert(!rangeStarted&&webStatus==409);cameraProof=true;cameraRange(false);assert(!rangeStarted);cameraRange(true);assert(rangeStarted);
 aq::Packet packet;packet.kind=aq::SCHEDULE;packet.session=77;packet.seq=pendingReply.seq=2;packet.t3=1;packet.t2=1;packet.offset=123;packet.uncertainty=500;
 childSchedule(packet,true);assert(childClock.ok&&syncOk&&syncOffset==123&&!armed);
 bodyRecordingActive=true;childClock.ok=false;childSchedule(packet,true);assert(childClock.ok&&!armed);bodyRecordingActive=false;
 calibrationActive=true;childClock.ok=false;childSchedule(packet,true);assert(childClock.ok&&!armed);calibrationActive=false;
 bodyCameraReadyAtomic=true;childSchedule(packet,true);assert(armed);armed=false;childSchedule(packet,false);assert(!armed);
 packet.t2=0;childClock.ok=false;childSchedule(packet,true);assert(childClock.ok&&!armed);packet.t2=1;
 packet.session=78;childClock.ok=false;childSchedule(packet,true);assert(!childClock.ok&&!armed);
 puts("PASS production start guard: no stop/create before warning; explicit acknowledgement, active-capture interlock, normal ready path and both GPS/PPS clocks required");
}
'''
path=r/'reports/raw/test_body_control.cpp';path.write_text(stub+clock+camera_guard+source+schedule_test+checks,encoding='utf-8')
target=path.with_suffix('.exe');zig=Path(ziglang.__file__).parent/('zig.exe' if sys.platform=='win32' else 'zig')
subprocess.run([str(zig),'c++','-std=c++17','-DIMU_HZ=100','-I'+str(r/'common'),str(path),'-o',str(target)],check=True)
subprocess.run([str(target)],check=True)
