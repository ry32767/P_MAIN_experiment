#pragma once
#include <stdint.h>
struct SdRecovery {
 bool wanted=true,pending=false,stable=false;uint32_t attempts=0,recoveries=0,since=0,healthySince=0;
 void manualStart(){wanted=true;pending=false;stable=false;attempts=0;}
 void stop(){wanted=false;pending=false;stable=false;}
 void failed(uint32_t now){stable=false;if(wanted){pending=true;since=now;}}
 void mounted(uint32_t now){if(attempts)++recoveries;pending=false;stable=true;healthySince=now;}
 void healthy(uint32_t now){if(stable && uint32_t(now-healthySince)>=30000)attempts=0;}
 bool due(uint32_t now)const{const uint32_t wait[]={5000,15000,60000};return wanted&&pending&&attempts<3&&uint32_t(now-since)>=wait[attempts];}
 void attempting(uint32_t now){++attempts;since=now;pending=false;}
 bool exhausted()const{return wanted&&pending&&attempts>=3;}
};
