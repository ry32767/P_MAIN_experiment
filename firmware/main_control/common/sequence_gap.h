#pragma once
#include <stdint.h>

namespace aq {

struct SequenceGapTracker {
  uint8_t last = 0;
  bool seen = false;

  uint32_t update(uint8_t sequence) {
    if (!seen) {
      last = sequence;
      seen = true;
      return 0;
    }
    const uint8_t delta = uint8_t(sequence - last);
    last = sequence;
    return delta > 1 ? uint32_t(delta - 1) : 0;
  }

  void reset() { seen = false; }
};

}  // namespace aq
