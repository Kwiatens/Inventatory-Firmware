// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// The log lines kept for sending over the air. The ring holds the newest lines: when it is full the oldest one is
// dropped, so logging never waits for the network. A line is cut at kLogLineBytes - 1 characters, and a byte
// outside printable ASCII is sent as '.', so a line never carries a terminal escape. No Arduino calls, so the
// host tests run the same code the device runs.

#include <cstddef>
#include <cstdint>

namespace inventatory_scan {

constexpr std::size_t kLogLineBytes = 160;
constexpr std::size_t kLogLineSlots = 48;

struct LogLine {
  uint32_t timestampMs = 0;
  std::size_t length = 0;
  char text[kLogLineBytes] = {};
};

class LogLineRing {
 public:
  // Appends a line, dropping the oldest one when every slot is taken.
  void push(const LogLine& line) {
    if (count_ == kLogLineSlots) {
      head_ = (head_ + 1U) % kLogLineSlots;
      --count_;
      ++dropped_;
    }
    lines_[(head_ + count_) % kLogLineSlots] = line;
    ++count_;
  }

  // Takes the oldest line. False when none is waiting.
  bool pop(LogLine& line) {
    if (count_ == 0) return false;
    line = lines_[head_];
    head_ = (head_ + 1U) % kLogLineSlots;
    --count_;
    return true;
  }

  std::size_t size() const { return count_; }
  // Lines dropped because the ring was full, since it was created.
  uint32_t dropped() const { return dropped_; }

 private:
  LogLine lines_[kLogLineSlots];
  std::size_t head_ = 0;
  std::size_t count_ = 0;
  uint32_t dropped_ = 0;
};

// Builds lines from a byte stream. A '\r' is dropped and a '\n' ends the line, so an empty line is kept. The line
// takes the time of its first byte.
class LogLineAssembler {
 public:
  void add(uint8_t byte, uint32_t timestampMs, LogLineRing& ring) {
    if (byte == '\r') return;
    if (pending_.length == 0) pending_.timestampMs = timestampMs;
    if (byte == '\n') {
      ring.push(pending_);
      pending_ = LogLine();
      return;
    }
    if (pending_.length + 1U >= kLogLineBytes) return;
    const bool printable = byte >= 0x20 && byte < 0x7F;
    pending_.text[pending_.length++] = printable ? static_cast<char>(byte) : '.';
  }

 private:
  LogLine pending_;
};

}  // namespace inventatory_scan
