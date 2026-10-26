#pragma once

#include <stdint.h>

namespace aq {

// Runtime SD failures are retried with a bounded backoff.  A blocked SD system
// call is handled separately by the hardware watchdog because no state machine
// can run while the calling task is stuck inside the driver.
class SpresenseSdRecovery {
public:
  bool wanted = true;
  bool pending = false;
  bool stable = false;
  uint32_t attempts = 0;
  uint32_t recoveries = 0;
  uint32_t failedAt = 0;
  uint32_t healthyAt = 0;

  static constexpr uint32_t retryDelayMs(uint32_t attempt) {
    return attempt == 0 ? 5000u : attempt == 1 ? 15000u : 60000u;
  }

  void manualStart() {
    wanted = true;
    pending = false;
    stable = false;
    attempts = 0;
  }

  void stop() {
    wanted = false;
    pending = false;
    stable = false;
  }

  void failed(uint32_t now) {
    stable = false;
    if (wanted) {
      pending = true;
      failedAt = now;
    }
  }

  bool due(uint32_t now) const {
    return wanted && pending && attempts < 3 &&
      uint32_t(now - failedAt) >= retryDelayMs(attempts);
  }

  void attempting(uint32_t now) {
    ++attempts;
    pending = false;
    failedAt = now;
  }

  void mounted(uint32_t now) {
    if (attempts) ++recoveries;
    pending = false;
    stable = true;
    healthyAt = now;
  }

  void healthy(uint32_t now) {
    if (stable && uint32_t(now - healthyAt) >= 30000u) attempts = 0;
  }

  bool exhausted() const {
    return wanted && pending && attempts >= 3;
  }
};

} // namespace aq
