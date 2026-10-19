// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// "Sync blocked" rule (AGENTS.md section 7): five refusals of the queue head in a row block it.
#pragma once

#include <cstdint>

namespace inventatory_scan {

class BlockedHeadTracker {
 public:
  static constexpr uint8_t kThreshold = 5;

  // Head event refused (authenticated 4xx/5xx, or a 200 without its ack).
  // True only on the refusal that blocks the queue.
  bool recordRefusal() {
    if (streak_ < kThreshold) ++streak_;
    if (streak_ == kThreshold && !blocked_) {
      blocked_ = true;
      return true;
    }
    return false;
  }

  // Head event acknowledged. True when this clears a block.
  bool recordProgress() {
    const bool wasBlocked = blocked_;
    streak_ = 0;
    blocked_ = false;
    return wasBlocked;
  }

  bool blocked() const { return blocked_; }
  uint8_t streak() const { return streak_; }

 private:
  uint8_t streak_ = 0;
  bool blocked_ = false;
};

}  // namespace inventatory_scan
