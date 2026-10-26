#include <cassert>
#include "sequence_gap.h"

int main() {
  aq::SequenceGapTracker tracker;
  assert(tracker.update(10) == 0);
  assert(tracker.update(11) == 0);
  assert(tracker.update(14) == 2);
  assert(tracker.update(255) == 240);
  assert(tracker.update(0) == 0);
  tracker.reset();
  assert(tracker.update(99) == 0);
}
