#pragma once
#include "protocol.h"
#include <limits>
namespace aqbin {
enum Kind:uint16_t {IMU=1,BNO=2,HEALTH=3,TIME_POSITION=4,SHOT=5,CAPTURE_BEGIN=6,ENVELOPE=7,COMPARATOR=8,CAPTURE_END=9,MAG_OBSERVATION=10,CALIBRATION_SET=11,GNSS_OBSERVATION=12,BODY_EVENT=13,BODY_PROFILE=14};
inline uint32_t crcUpdate(uint32_t c,const void* p,size_t n){auto b=static_cast<const uint8_t*>(p);while(n--){c^=*b++;for(int i=0;i<8;i++)c=(c>>1)^(0xedb88320u & (0u-(c&1)));}return c;}
#pragma pack(push,1)
struct FileHeader {char magic[8]={'A','Q','B','I','N','0','0','1'};uint16_t version=1,bytes=32;uint8_t role=0,stream=0;uint16_t reserved=0;uint32_t session=0;uint64_t createdUs=0;uint32_t crc=0;};
struct RecordHeader {uint32_t magic=0x52425141;uint16_t kind=0,bytes=0;uint64_t monoUs=0;uint32_t sequence=0,crc=0;};
struct Imu {uint32_t index,fresh;uint64_t scheduled,acquired,source,utc;uint8_t utcSource;float values[13];};
struct Bno {uint64_t acquired,source,utc;uint32_t sequence;uint8_t sensor,utcSource;float values[4];};
struct Health {aq::Health sensor;uint32_t linkRx;uint8_t sync;int64_t offset;uint32_t uncertainty,shot;uint64_t reference,acoustic;int64_t tof;uint32_t state;char rtc[24];float rtcTemperature;uint64_t utcMono,utcEpoch,utcUntil;uint8_t utcSource;uint32_t frequency;uint16_t observationSeconds,observationEvery;uint8_t rate,pattern,patternId;uint16_t patternGap1,patternGap2;};
struct TimePosition {uint64_t utcMs;int64_t phoneAgeMs;uint32_t rtt;uint8_t source,positionValid;int32_t latitude,longitude;uint32_t accuracyMm;};
struct Shot {uint32_t session,shot,frequency,state;uint64_t parentScheduled,reference,acoustic;int64_t tof;uint8_t observation;uint16_t observationSeconds;uint8_t rate,pattern,patternId;uint16_t patternGap1,patternGap2;};
struct CaptureBegin {uint32_t session,shot,frequency,sampleRate;uint64_t reference,arrival,adcStart;uint32_t firstIndex,targetSamples;uint16_t seconds;};
struct EnvelopePrefix {uint32_t shot,index;uint16_t count,periodUs;};
struct Edge {uint32_t shot;uint8_t high;};
enum EndReason:uint8_t {COMPLETE=0,STOPPED=1,NO_ARRIVAL=2,UNAVAILABLE=3,SD_FAILURE=4};
struct MagObservation {uint32_t sequence;uint64_t begin,end,utc;uint32_t calibration,set,uncertainty=UINT32_MAX;uint16_t utcSource=0,valid=0;float raw[3],corrected[3],body[3];uint32_t bodyValid=0;};
struct GnssObservation {uint32_t itow;uint64_t received,utc;uint8_t utcSource,fix,flags,flags2;int32_t latitude,longitude,height,vn,ve,vd;uint32_t hAcc,vAcc,sAcc,set;};
struct CaptureEnd {uint32_t shot,writtenSamples,lostSamples,edgeDrops,requestDrops;uint8_t reason;};
#pragma pack(pop)
static_assert(sizeof(FileHeader)==32 && sizeof(RecordHeader)==24,"Binary framing");
static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559,"IEEE754 float required");
inline void seal(FileHeader& h){h.crc=aq::crc32(&h,sizeof(h)-4);}
inline bool valid(const FileHeader& h){return !memcmp(h.magic,"AQBIN001",8) && h.version==1 && h.bytes==32 && h.crc==aq::crc32(&h,28);}
inline void seal(RecordHeader& h,const void* payload){h.crc=~crcUpdate(crcUpdate(~0u,&h,20),payload,h.bytes);}
inline bool valid(const RecordHeader& h,const void* payload){return h.magic==0x52425141 && h.crc==~crcUpdate(crcUpdate(~0u,&h,20),payload,h.bytes);}
}
