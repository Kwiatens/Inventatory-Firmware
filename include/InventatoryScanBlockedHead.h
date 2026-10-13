// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// The "Sync blocked" rule (AGENTS.md section 7): after five refusals of the queue's
// head event in a row, the operator is told the queue is stuck. Pure logic, so the
// host suite can check the rule directly.
#pragma once

#include <cstdint>

namespace inventatory_scan {

class BlockedHeadTracker {
 public:
  static constexpr uint8_t kThreshold = 5;

  // The head event was refused: an authenticated 4xx or 5xx response, or a 200
  // response that did not acknowledge it. Returns true on the refusal that blocks
  // the queue, so the caller can update the screen once.
  bool recordRefusal() {
    if (streak_ < kThreshold) ++streak_;
    if (streak_ == kThreshold && !blocked_) {
      blocked_ = true;
      return true;
    }
    return false;
  }

  // The head event was acknowledged, so the queue is moving again. Returns true when
  // this clears a block, so the caller can update the screen once.
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
