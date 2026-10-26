#pragma once
#include <stdint.h>
namespace aq {
constexpr uint32_t frequencies[]={10000,50000,100000,200000};
constexpr uint32_t envelopeRate=20000,envelopePeriodUs=1000000/envelopeRate;
struct AcquisitionConfig {uint32_t frequency=200000;uint16_t seconds=3,every=30;uint8_t rate=1,pattern=0,patternId=0;};
constexpr uint8_t patternCount=64,noPattern=255;
constexpr uint32_t patternToleranceUs=200;
// Disjoint gap bands and 600us steps keep +/-200us acceptance bands apart.
inline uint16_t patternGapUs(uint8_t id,unsigned index){return uint16_t((index?7000:1000)+600*(index?id/8:id%8));}
inline uint8_t nextPatternId(uint8_t id){return uint8_t((id+17)%patternCount);}
constexpr uint64_t patternReuseUs=16000000;
struct PatternSequence {
 uint8_t cursor;uint64_t lastUsed[patternCount]{};
 explicit PatternSequence(uint8_t seed):cursor(seed%patternCount){}
 bool available(uint64_t planned)const{return !lastUsed[cursor] || (planned>=lastUsed[cursor] && planned-lastUsed[cursor]>=patternReuseUs);}
 void reserve(uint64_t planned){lastUsed[cursor]=planned;cursor=nextPatternId(cursor);}
};
inline bool validShotPattern(const AcquisitionConfig& c,uint16_t gap1,uint16_t gap2){
 return c.pattern?(c.patternId<patternCount && gap1==patternGapUs(c.patternId,0) && gap2==patternGapUs(c.patternId,1)):(c.patternId==noPattern && !gap1 && !gap2);
}
inline bool supportedFrequency(uint32_t hz){for(auto f:frequencies)if(f==hz)return true;return false;}
inline bool validConfig(const AcquisitionConfig& c){return supportedFrequency(c.frequency) && c.seconds>=1 && c.seconds<=5 && c.every<=300 && c.rate>=1 && c.rate<=4 && c.pattern<=1;}
inline uint32_t shotIntervalUs(const AcquisitionConfig& c){return 1000000/c.rate;}
// PIO: five periods, gap in whole periods, three pull/out instructions.
inline uint32_t patternSpacingUs(uint32_t hz,unsigned gap){return (uint64_t(5)*4000000+uint64_t(gap)*hz*4+3000000)/(4*hz);}
inline uint32_t patternTailUs(const AcquisitionConfig& c){return c.pattern?patternSpacingUs(c.frequency,patternGapUs(c.patternId,0))+patternSpacingUs(c.frequency,patternGapUs(c.patternId,1))+600:0;}
inline bool observeShot(uint32_t index,const AcquisitionConfig& c,bool recordingReady=true){return recordingReady && c.every && index%c.every==0;}
inline uint64_t observationHoldUs(const AcquisitionConfig& c){return uint64_t(c.seconds)*1000000+500000;}
// All indices carry actual DMA sample numbers; missing samples are never compressed away.
inline uint32_t firstEnvelopeIndex(uint64_t start,uint64_t arrival){return arrival<=start?0:uint32_t((arrival-start+envelopePeriodUs-1)/envelopePeriodUs);}
struct EnvelopeRead {uint32_t index,lost,count;};
inline EnvelopeRead envelopeRead(uint32_t read,uint32_t produced,uint32_t target,uint32_t capacity,uint32_t block){
 EnvelopeRead result{read,0,0};
 if(read>=target || produced<read)return result;
 if(produced-read>capacity){result.index=produced-capacity;if(result.index>target)result.index=target;result.lost=result.index-read;}
 uint32_t end=produced<target?produced:target;
 result.count=end-result.index;if(result.count>block)result.count=block;
 if(result.count<block && produced<target)result.count=0;
 return result;
}
enum CaptureAction:uint8_t {CAPTURE_ARM=1,CAPTURE_TRIGGER=2};
struct CaptureRequest {uint64_t reference=0,arrival=0;uint32_t session=0,shot=0,frequency=0;uint16_t seconds=0;uint8_t action=0;};
struct CaptureEdge {uint64_t us;uint32_t shot;uint8_t high;};
}
