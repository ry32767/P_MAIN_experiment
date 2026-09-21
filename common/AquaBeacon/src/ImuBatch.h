#pragma once
#include <AquaBeacon.h>
#include <stdio.h>
#include <ImuUtc.h>
namespace aqimu {
constexpr uint32_t rate = 960;
constexpr unsigned batchSize = 32;
constexpr uint32_t magic = 0x494d5532;
struct Data { uint32_t timestamp; float temp,gx,gy,gz,ax,ay,az; };
struct Row { uint64_t received_us; uint32_t seq; Data data; Stamp utc; };
struct Batch {
  uint32_t version, id, count, length, error;
  Row rows[batchSize];
  char csv[batchSize*320];
};
// Identical layout on MainCore/SubCore; MP messages transfer ownership.
inline bool format(Batch &b) {
  b.length=0; b.error=1;
  if(b.version!=magic || b.count>batchSize || !b.count) return false;
  for(unsigned i=0;i<b.count;++i) {
    const auto &r=b.rows[i]; const auto &d=r.data; char line[320];
    int n=snprintf(line,sizeof(line),"%lu,%llu,%lu,%.6f,%.8f,%.8f,%.8f,%.8f,%.8f,%.8f,%llu,%u,%u,%lu,%llu,%lu",
      (unsigned long)r.seq,(unsigned long long)r.received_us,(unsigned long)d.timestamp,
      d.temp,d.gx,d.gy,d.gz,d.ax,d.ay,d.az,
      (unsigned long long)r.utc.utc_us,unsigned(r.utc.source!=unknown),unsigned(r.utc.sync_valid),
      (unsigned long)r.utc.source,(unsigned long long)r.utc.age_us,(unsigned long)r.utc.anchor_seq);
    if(n<0 || unsigned(n)+10>=sizeof(line)) return false;
    uint32_t crc=aq::crc32((const uint8_t*)line,n);
    int extra=snprintf(line+n,sizeof(line)-n,",%08lx\n",(unsigned long)crc);
    if(extra!=10 || b.length+n+extra>sizeof(b.csv)) return false;
    memcpy(b.csv+b.length,line,n+extra); b.length+=n+extra;
  }
  b.error=0; return true;
}
}
