#include <ImuBatch.h>
#include <cassert>
#include <cstdlib>
#include <cfloat>
#include <string>
int main() {
  aqimu::Batch b{}; b.version=aqimu::magic; b.count=aqimu::batchSize;
  for(unsigned i=0;i<b.count;++i) { b.rows[i].seq=i+1; b.rows[i].received_us=1000000+i*1042; b.rows[i].data.timestamp=0xffff0000u+i*20000; b.rows[i].data.az=9.8f; }
  assert(aqimu::format(b) && !b.error && b.length<=sizeof(b.csv));
  std::string csv(b.csv,b.length); size_t at=0; unsigned rows=0;
  while(at<csv.size()) { size_t end=csv.find('\n',at); assert(end!=std::string::npos); size_t comma=csv.rfind(',',end); assert(comma>=at); auto crc=strtoul(csv.substr(comma+1,end-comma-1).c_str(),nullptr,16); assert(crc==aq::crc32((const uint8_t*)csv.data()+at,comma-at)); at=end+1; ++rows; }
  assert(rows==aqimu::batchSize);
  b.count=aqimu::batchSize+1; assert(!aqimu::format(b));
  b.count=0; assert(!aqimu::format(b));
  b.count=1;b.version=0;assert(!aqimu::format(b));
  b.version=aqimu::magic;auto &d=b.rows[0].data;d.temp=d.gx=d.gy=d.gz=d.ax=d.ay=d.az=FLT_MAX;assert(!aqimu::format(b));
  puts("PASS: full batch CRC, sequence, timestamp wrap, bounds and format overflow");
}
