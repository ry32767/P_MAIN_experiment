#include "spresense_protocol.h"
#include <cassert>
#include <cstdio>
#include <vector>
using namespace spnav;
Nav nav(uint32_t seq,uint64_t epoch) { Nav v; v.sequence=seq; v.utc_s=epoch; v.flags=time_valid|position_valid; v.lat_e7=-350000000; v.lon_e7=1390000000; v.fix=3; return v; }
int main() {
  assert(crc32((const uint8_t*)"123456789",9)==0xcbf43926u);
  auto n=nav(42,1800000000); n.imu_gaps=14; n.altitude_mm=-3100;
  uint8_t b[frame_size]; encode(n,b); Nav got;
  assert(decode(b,got) && got.lat_e7==n.lat_e7 && got.altitude_mm==-3100 && got.imu_gaps==14);
  for(size_t i=0;i<frame_size;i++) { b[i]^=0x40; assert(!decode(b,got)); b[i]^=0x40; }
  Parser parser; int decoded=0;
  for(int i=0;i<99;i++) parser.push(0xaa,got);
  for(size_t i=0;i<frame_size/2;i++) parser.push(b[i],got);
  for(auto c:b) if(parser.push(c,got)) decoded++;
  assert(decoded==1 && got.sequence==42);
  uint32_t random=1;
  for(int i=0;i<20000;i++) { random=random*1664525+1013904223; parser.push(uint8_t(random>>24),got); }
  decoded=0; for(auto c:b) if(parser.push(c,got)) decoded++; assert(decoded==1);
  n.lat_e7=1000000000; encode(n,b); assert(!decode(b,got));
  n=nav(1,1800000000); n.utc_s=1; encode(n,b); assert(!decode(b,got));
  Clock c;
  for(unsigned i=0;i<3;i++) { c.pps((i+1)*1000000ULL); assert(c.accept(nav(i+1,1800000000+i),(i+1)*1000000ULL+200000)); }
  assert(c.locked(3200000) && c.utc(3250000)==1800000002250000ULL);
  assert(!c.locked(4500000) && c.utc(4500000)==0);
  assert(!c.accept(nav(3,1800000002),3300000)); // Duplicate must not advance anchor.
  c.pps(4000000); assert(!c.accept(nav(4,1800000003),4900000)); assert(!c.locked(4900000));
  for(unsigned i=0;i<3;i++) { c.pps((i+5)*1000000ULL); c.accept(nav(i+5,1800000004+i),(i+5)*1000000ULL+100000); }
  assert(c.locked(7100000));
  c.pps(7500000); assert(!c.locked(7510000)); // Spurious half-second PPS.
  c.pps(8000000); c.accept(nav(8,1800000007),8100000); assert(!c.locked(8100000));
  c.pps(9000000); n=nav(9,1800000100); c.accept(n,9100000); assert(!c.locked(9100000)); // UTC jump.
  c.invalidate(); assert(!c.locked(9100000));
  Clock wrap;
  for(unsigned i=0;i<3;i++) { wrap.pps(4294000000ULL+i*1000000ULL); wrap.accept(nav(0xfffffffeu+i,1800000000+i),4294200000ULL+i*1000000ULL); }
  assert(wrap.locked(4296200000ULL)); // 32-bit micros and sequence wrap.
  n=nav(2,1800000003); n.flags=0; wrap.pps(4297000000ULL); assert(!wrap.accept(n,4297200000ULL)); assert(!wrap.locked(4297200000ULL));
  assert(unix_seconds(2026,9,19,0,0,0)==1789776000ULL);
  assert(unix_seconds(2024,2,29,23,59,59)+1==unix_seconds(2024,3,1,0,0,0));
  assert(!unix_seconds(2025,2,29,0,0,0)); assert(!unix_seconds(2024,1,1,0,0,60));
  assert(!unix_seconds(2026,13,1,0,0,0));
  puts("PASS: CRC, frame corruption/resync, coordinate/date bounds, PPS lock/timeout/duplicates/jumps, sequence and 64-bit time wrap");
}
