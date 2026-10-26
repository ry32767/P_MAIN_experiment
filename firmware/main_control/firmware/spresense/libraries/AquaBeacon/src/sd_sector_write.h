#pragma once
#include <stddef.h>
#include <stdint.h>
namespace aqsd {
// Keep each FAT write inside one SD sector. In the Sony 3.4.7 integration,
// multi-sector writes time out when CXD5602PWBIMU acquisition is running.
// Respect the current offset: CSV headers and reference rows are not aligned.
constexpr size_t sectorBytes=512;
inline size_t writeLimit(uint64_t offset,size_t remaining){
 const size_t boundary=sectorBytes-size_t(offset%sectorBytes);
 return remaining<boundary?remaining:boundary;
}
}
