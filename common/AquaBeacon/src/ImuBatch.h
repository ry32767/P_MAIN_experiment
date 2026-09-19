#pragma once
#include <AquaBeacon.h>
#include <stdio.h>
namespace aqimu {
constexpr uint32_t rate = 960;
constexpr unsigned batchSize = 32;
constexpr uint32_t magic = 0x494d5531;
struct Data { uint32_t timestamp; float temp,gx,gy,gz,ax,ay,az; };
struct Row { uint64_t received_us; uint32_t seq; Data data; };
struct Batch {
  uint32_t version, id, count, length, error;
  Row rows[batchSize];
  char csv[batchSize*240];
};
// Identical layout on MainCore/SubCore; MP messages transfer ownership.
inline bool format(Batch &b) {
  b.length=0; b.error=1;
  if(b.version!=magic || b.count>batchSize || !b.count) return false;
  for(unsigned i=0;i<b.count;++i) {
    const auto &r=b.rows[i]; const auto &d=r.data; char line[240];
    int n=snprintf(line,sizeof(line),"%lu,%llu,%lu,%.6f,%.8f,%.8f,%.8f,%.8f,%.8f,%.8f",
      (unsigned long)r.seq,(unsigned long long)r.received_us,(unsigned long)d.timestamp,
      d.temp,d.gx,d.gy,d.gz,d.ax,d.ay,d.az);
    if(n<0 || unsigned(n)+10>=sizeof(line)) return false;
    uint32_t crc=aq::crc32((const uint8_t*)line,n);
    int extra=snprintf(line+n,sizeof(line)-n,",%08lx\n",(unsigned long)crc);
    if(extra!=10 || b.length+n+extra>sizeof(b.csv)) return false;
    memcpy(b.csv+b.length,line,n+extra); b.length+=n+extra;
  }
  b.error=0; return true;
}
}
