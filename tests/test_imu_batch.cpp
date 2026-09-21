#include <ImuBatch.h>
#include <cassert>
#include <cstdlib>
#include <cfloat>
#include <string>
int main() {
  aqimu::UtcClock clock;
  constexpr uint64_t second=1790000000ULL;
  assert(clock.at(1,{}).source==aqimu::unknown);
  clock.update(true,second,0,1,1020000,{1000000,1});
  auto stamp=clock.at(1021234,{1000000,1});
  assert(stamp.source==aqimu::gnss_rx_estimate && !stamp.sync_valid);
  assert(stamp.utc_us==second*1000000+1234);
  clock.update(true,second+1,0,2,2020000,{2000000,2});
  clock.update(true,second+2,0,3,3020000,{3000000,3});
  stamp=clock.at(3021234,{3000000,3});
  assert(stamp.source==aqimu::gnss_pps_candidate && stamp.sync_valid);
  assert(stamp.utc_us==(second+2)*1000000+21234 && stamp.anchor_seq==3);
  assert(clock.at(3020000-1,{3000000,3}).source==aqimu::unknown);
  assert(clock.at(4520001,{4000000,4}).source==aqimu::unknown);
  assert(clock.at(3021234,{3021200,5}).source==aqimu::unknown);
  clock.update(false,second+3,0,4,4020000,{4000000,4});
  assert(clock.at(4020001,{4000000,4}).source==aqimu::unknown);
  clock.update(true,second+4,0,5,5020000,{});
  assert(clock.at(5020001,{}).source==aqimu::gnss_rx_estimate);
  clock.update(true,second+5,1001,6,6020000,{6000000,6});
  assert(clock.at(6020001,{6000000,6}).source==aqimu::unknown);
  // Duplicate pulse, skipped UTC second and irregular period cannot lock.
  clock.update(true,second,0,1,1020000,{1000000,1});
  clock.update(true,second+1,0,2,1120000,{1000000,1});
  clock.update(true,second+3,0,3,2020000,{2000000,2});
  clock.update(true,second+4,0,4,2520000,{2500000,3});
  assert(!clock.at(2520001,{2500000,3}).sync_valid);
  aqimu::Batch b{}; b.version=aqimu::magic; b.count=aqimu::batchSize;
  for(unsigned i=0;i<b.count;++i) { b.rows[i].seq=i+1; b.rows[i].received_us=1000000+i*1042; b.rows[i].data.timestamp=0xffff0000u+i*20000; b.rows[i].data.az=9.8f; b.rows[i].utc=stamp; }
  assert(aqimu::format(b) && !b.error && b.length<=sizeof(b.csv));
  std::string csv(b.csv,b.length); size_t at=0; unsigned rows=0;
  while(at<csv.size()) { size_t end=csv.find('\n',at); assert(end!=std::string::npos); size_t comma=csv.rfind(',',end); assert(comma>=at); auto crc=strtoul(csv.substr(comma+1,end-comma-1).c_str(),nullptr,16); assert(crc==aq::crc32((const uint8_t*)csv.data()+at,comma-at)); at=end+1; ++rows; }
  assert(rows==aqimu::batchSize);
  assert(csv.find(",1790000002021234,1,1,2,1234,3,")!=std::string::npos);
  b.count=aqimu::batchSize+1; assert(!aqimu::format(b));
  b.count=0; assert(!aqimu::format(b));
  b.count=1;b.version=0;assert(!aqimu::format(b));
  b.version=aqimu::magic;auto &d=b.rows[0].data;d.temp=d.gx=d.gy=d.gz=d.ax=d.ay=d.az=FLT_MAX;assert(!aqimu::format(b));
  puts("PASS: full batch CRC, sequence, timestamp wrap, bounds and format overflow");
}
