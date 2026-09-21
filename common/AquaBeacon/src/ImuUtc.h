#pragma once
#include <stdint.h>
namespace aqimu {
// UTC describes host readout, not the sensor's internal sampling instant.
enum Source : uint32_t { unknown=0, gnss_rx_estimate=1, gnss_pps_candidate=2 };
struct Stamp {
  uint64_t utc_us=0, age_us=0;
  uint32_t source=unknown, sync_valid=0, anchor_seq=0;
};
struct Edge {
  uint64_t us; uint32_t count;
  Edge(uint64_t timestamp=0, uint32_t sequence=0):us(timestamp),count(sequence) {}
};
class UtcClock {
  uint64_t anchorMono=0, anchorUtc=0, received=0, previousSecond=0;
  uint32_t seq=0, streak=0, source=unknown;
  Edge previous{};
public:
  void update(bool valid, uint64_t second, uint32_t usec, uint32_t sequence,
              uint64_t rx, Edge edge) {
    source=unknown;
    if(!valid || !second || usec>1000) { streak=0; previous={}; return; }
    received=rx; seq=sequence; anchorMono=rx;
    anchorUtc=second*1000000ULL+usec; source=gnss_rx_estimate;
    // The preceding PPS is a candidate only in a bounded notification window.
    bool paired=edge.count && rx>=edge.us && rx-edge.us>=2000 && rx-edge.us<=500000;
    if(paired) {
      bool consecutive=previous.count && uint32_t(edge.count-previous.count)==1 &&
        second==previousSecond+1 && edge.us>previous.us &&
        edge.us-previous.us>=990000 && edge.us-previous.us<=1010000;
      streak=consecutive ? (streak<3 ? streak+1 : 3) : 1;
      if(streak>=3) {
        source=gnss_pps_candidate; anchorMono=edge.us; anchorUtc=second*1000000ULL;
      }
      previous=edge; previousSecond=second;
    } else { streak=0; previous={}; }
  }
  Stamp at(uint64_t now, Edge latest) const {
    Stamp s;
    if(source==unknown || now<received || now-received>1500000 || now<anchorMono) return s;
    // Extra/spurious edges invalidate the PPS association until reacquired.
    if(source==gnss_pps_candidate && (latest.us>now || now-latest.us>1500000 ||
       uint32_t(latest.count-previous.count)>1)) return s;
    s.utc_us=anchorUtc+(now-anchorMono); s.age_us=now-received;
    s.source=source; s.sync_valid=source==gnss_pps_candidate; s.anchor_seq=seq;
    return s;
  }
};
}
