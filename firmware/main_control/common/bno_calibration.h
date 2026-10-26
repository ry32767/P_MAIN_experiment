#pragma once
#include <stdint.h>
namespace aq {
// Sensor-reported accuracy is a progress indicator, not measured angular accuracy.
struct BnoCalibration {
 bool active=false,saved=false;
 uint8_t accuracy[3]{},previousMask=0;
 uint64_t seen[3]{},started=0,goodSince=0;
 static constexpr uint64_t lifetimeUs=600000000, freshUs=500000, settleUs=5000000;
 void clear(){*this=BnoCalibration{};}
 void start(uint64_t now,uint8_t mask){clear();active=true;started=now;previousMask=mask;}
 void event(unsigned sensor,uint8_t status,uint64_t now){if(sensor<3){accuracy[sensor]=status&3;seen[sensor]=now;}}
 bool fresh(unsigned sensor,uint64_t now)const{return sensor<3 && seen[sensor] && now>=seen[sensor] && now-seen[sensor]<freshUs;}
 bool good(uint64_t now)const{for(unsigned i=0;i<3;i++)if(!fresh(i,now)||accuracy[i]<2)return false;return true;}
 void tick(uint64_t now){if(!active||!good(now))goodSince=0;else if(!goodSince)goodSince=now;}
 bool ready(uint64_t now)const{return active && good(now) && goodSince && now>=goodSince && now-goodSince>=settleUs;}
 bool expired(uint64_t now)const{return active && now-started>=lifetimeUs;}
};
}
