#pragma once
#include "protocol.h"
#include "acquisition.h"
namespace aq {
// Whole ranging transactions and control exchanges serialize the half-duplex bus.
inline bool canStartProbe(uint64_t now,uint64_t next,uint64_t result,uint64_t pending,bool control){return now>=next && !result && !pending && !control;}
inline bool shotMetadataMatches(const Packet& result,const Packet& planned){return result.frequency==planned.frequency && result.pattern==planned.pattern && result.patternId==planned.patternId && result.patternGap1==planned.patternGap1 && result.patternGap2==planned.patternGap2;}
constexpr uint8_t shotMarker=0x00;
constexpr uint64_t markerTimeoutUs=500000;
// RESULT t3 is the child's tether edge, t2 its acoustic edge. No clock offset.
inline int64_t localTof(uint64_t reference,uint64_t acoustic){
 return acoustic && reference?int64_t(acoustic)-int64_t(reference):0;
}
inline uint32_t rangeStatus(uint64_t reference,uint64_t acoustic,bool invalid,bool high){
 if(invalid || !reference || (acoustic && (acoustic<reference || acoustic-reference>windowUs)))return 4;
 if(!acoustic)return high?5:1;
 return acoustic-reference<earlyUs?2:3;
}
struct AcousticWindow {
 uint64_t reference=0,rise=0,confirmed=0;
 AcquisitionConfig config;uint64_t candidates[64]{};unsigned count=0;bool overflow=false;
 void start(uint64_t t,const AcquisitionConfig& c={}){reference=t;rise=confirmed=0;config=c;count=0;overflow=false;}
 bool near(uint64_t a,uint64_t b,uint32_t spacing){return b>=a && b-a+patternToleranceUs>=spacing && b-a<=spacing+patternToleranceUs;}
 void pulse(uint64_t t){
  if(confirmed)return;
  if(!config.pattern){confirmed=t;return;}
  for(unsigned i=0;i<count;i++)for(unsigned j=i+1;j<count;j++)
   if(candidates[i]-reference<=windowUs && near(candidates[i],candidates[j],patternSpacingUs(config.frequency,patternGapUs(config.patternId,0))) && near(candidates[j],t,patternSpacingUs(config.frequency,patternGapUs(config.patternId,1)))){confirmed=candidates[i];return;}
  if(count<64)candidates[count++]=t;else overflow=true;
 }
 void edge(uint64_t t,bool high){
  if(!reference || t<reference || t-reference>windowUs+patternTailUs(config))return;
  if(high){if(!rise)rise=t;}
  else {if(rise && t>=rise && t-rise>=6 && (!config.pattern || t-rise<=600))pulse(rise);rise=0;}
 }
};
}
