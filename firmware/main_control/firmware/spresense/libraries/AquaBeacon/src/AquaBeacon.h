#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
namespace aq {
constexpr size_t frame_size = 64;
enum Flag : uint8_t { time_valid = 1, position_valid = 2, sd_ok = 4, imu_ok = 8 };
struct Nav {
  uint8_t flags = 0, fix = 0, satellites = 0;
  uint32_t sequence = 0;
  uint64_t utc_s = 0;
  int32_t lat_e7 = 0, lon_e7 = 0, altitude_mm = 0;
  uint32_t usec = 0, imu_samples = 0, imu_errors = 0, sd_errors = 0;
  uint32_t uptime_ms = 0, sd_rows = 0, imu_gaps = 0;
};
inline uint32_t crc32(const uint8_t *p, size_t n) {
  uint32_t c = ~0u;
  while (n--) { c ^= *p++; for (int i=0;i<8;i++) c = (c >> 1) ^ (0xedb88320u & (0u-(c&1))); }
  return ~c;
}
inline void put32(uint8_t *p, uint32_t v) { for(int i=0;i<4;i++) p[i]=uint8_t(v>>(8*i)); }
inline uint32_t get32(const uint8_t *p) { return uint32_t(p[0]) | uint32_t(p[1])<<8 | uint32_t(p[2])<<16 | uint32_t(p[3])<<24; }
inline void encode(const Nav &v, uint8_t *b) {
  memset(b,0,frame_size); memcpy(b,"AQB1",4); b[4]=1; b[5]=v.flags; b[6]=v.fix; b[7]=v.satellites;
  put32(b+8,v.sequence); put32(b+12,uint32_t(v.utc_s)); put32(b+16,uint32_t(v.utc_s>>32));
  put32(b+20,uint32_t(v.lat_e7)); put32(b+24,uint32_t(v.lon_e7)); put32(b+28,uint32_t(v.altitude_mm));
  put32(b+32,v.usec); put32(b+36,v.imu_samples); put32(b+40,v.imu_errors); put32(b+44,v.sd_errors);
  put32(b+48,v.uptime_ms); put32(b+52,v.sd_rows); put32(b+56,v.imu_gaps); put32(b+60,crc32(b,60));
}
inline bool decode(const uint8_t *b, Nav &v) {
  if(memcmp(b,"AQB1",4) || b[4]!=1 || (b[5]&0xf0) || get32(b+60)!=crc32(b,60)) return false;
  v.flags=b[5]; v.fix=b[6]; v.satellites=b[7]; v.sequence=get32(b+8);
  v.utc_s=get32(b+12) | uint64_t(get32(b+16))<<32;
  v.lat_e7=int32_t(get32(b+20)); v.lon_e7=int32_t(get32(b+24)); v.altitude_mm=int32_t(get32(b+28));
  v.usec=get32(b+32); v.imu_samples=get32(b+36); v.imu_errors=get32(b+40); v.sd_errors=get32(b+44);
  v.uptime_ms=get32(b+48); v.sd_rows=get32(b+52); v.imu_gaps=get32(b+56);
  return v.usec<1000000 && (!(v.flags&time_valid) || (v.utc_s>=1577836800ULL && v.utc_s<4102444800ULL))
    && (!(v.flags&position_valid) || (v.lat_e7>=-900000000 && v.lat_e7<=900000000 && v.lon_e7>=-1800000000 && v.lon_e7<=1800000000));
}
class Parser {
  uint8_t buf[frame_size]{}; size_t used=0;
public:
  uint32_t errors=0;
  void reset() { used=0; }
  bool push(uint8_t c, Nav &v) {
    buf[used++]=c;
    if(used<frame_size) return false;
    if(decode(buf,v)) { used=0; return true; }
    if(!memcmp(buf,"AQB1",4)) ++errors;
    memmove(buf,buf+1,--used); return false;
  }
};
// UTC labels refer to the preceding PPS. A 1-second systematic association
// error cannot be detected by continuity checks: bench verification is required.
class Clock {
  uint64_t edge=0, previous=0, anchor=0, epoch=0, packet_at=0;
  uint32_t edge_id=0, last_id=0, last_seq=0, period=1000000;
  unsigned good=0;
public:
  uint32_t rejected=0, interval_us=0, uart_age_us=0;
  void invalidate() { good=0; }
  void pps(uint64_t t) {
    previous=edge; edge=t; ++edge_id;
    interval_us=previous ? uint32_t(edge-previous) : 0;
    if(previous && (edge-previous<999000 || edge-previous>1001000)) good=0;
    else if(previous) period=interval_us;
  }
  bool accept(const Nav &n, uint64_t received) {
    uint64_t age=received>=edge ? received-edge : UINT64_MAX;
    uart_age_us=uint32_t(age);
    if(!(n.flags&time_valid) || !edge || age<20000 || age>800000 || n.usec>1000) {
      good=0; ++rejected; return false;
    }
    if(good && (edge_id==last_id || n.sequence==last_seq)) { ++rejected; return false; }
    bool consecutive=good && edge_id==last_id+1 && n.utc_s==epoch+1 && n.sequence==last_seq+1;
    good=consecutive ? (good<3 ? good+1 : 3) : 1;
    anchor=edge; epoch=n.utc_s; last_id=edge_id; last_seq=n.sequence; packet_at=received;
    return true;
  }
  bool locked(uint64_t now) const {
    return good>=3 && now>=anchor && now-anchor<1500000 && now>=packet_at && now-packet_at<1500000;
  }
  uint64_t utc(uint64_t now) const {
    return locked(now) ? epoch*1000000ULL + (now-anchor)*1000000ULL/period : 0;
  }
  unsigned streak() const { return good; }
};
inline uint64_t unix_seconds(int y,int m,int d,int h,int min,int s) {
  static const int lengths[]={31,28,31,30,31,30,31,31,30,31,30,31};
  if(y<2020 || y>2099 || m<1 || m>12 || d<1 || d>lengths[m-1]+(m==2 && y%4==0) || h<0 || h>23 || min<0 || min>59 || s<0 || s>59) return 0;
  uint64_t days=0;
  for(int year=1970;year<y;year++) days+=365+(year%4==0 && (year%100!=0 || year%400==0));
  for(int month=1;month<m;month++) days+=lengths[month-1]+(month==2 && y%4==0);
  return ((days+d-1)*24+h)*3600+min*60+s;
}
}
