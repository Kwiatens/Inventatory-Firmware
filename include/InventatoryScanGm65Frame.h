// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Inventatory Scan R1 GM65 frame assembly and register CRC. Pure logic with no
// hardware access, so the host suite can drive it with replayed byte streams.
#pragma once

#include <cstddef>
#include <cstdint>

namespace inventatory_scan {

// A scan frame carries at most this many payload bytes (AGENTS.md invariant 9).
constexpr std::size_t kGm65MaxFrameBytes = 1024U;
// A partial frame whose bytes stop arriving for this long is dropped. It is never
// emitted as a scan, because the rest of the barcode would then arrive as a second,
// wrong scan.
constexpr uint32_t kGm65FrameIdleTimeoutMs = 30U;

// CRC-16/XMODEM (polynomial 0x1021, initial value 0, no reflection), used by the
// module's register protocol. The check value for "123456789" is 0x31C3.
inline uint16_t gm65Crc16(const uint8_t* bytes, std::size_t length) {
  uint16_t crc = 0x0000U;
  for (std::size_t index = 0; index < length; ++index) {
    crc = static_cast<uint16_t>(crc ^ (static_cast<uint16_t>(bytes[index]) << 8U));
    for (uint8_t bit = 0; bit < 8U; ++bit) {
      crc = (crc & 0x8000U) != 0U ? static_cast<uint16_t>((crc << 1U) ^ 0x1021U)
                                  : static_cast<uint16_t>(crc << 1U);
    }
  }
  return crc;
}

// Printable ASCII, plus the group, record and end-of-transmission separators that
// ISO 15434 messages use. Any other byte marks the frame as corrupt.
inline bool gm65IsPayloadByte(uint8_t byte) {
  return (byte >= 0x20U && byte <= 0x7EU) || byte == 0x1DU || byte == 0x1EU || byte == 0x04U;
}

class Gm65FrameAssembler {
 public:
  enum class Event : uint8_t {
    None,     // nothing to report
    Frame,    // a complete frame is ready; read it with frame() and frameLength()
    Dropped,  // a partial, oversized or corrupt frame was discarded
  };

  // Feeds one byte received at nowMs (a millisecond clock). A frame that was ready
  // from an earlier call is released first, and a partial frame that has gone idle
  // is dropped before the byte is considered, so a stale fragment can never join it.
  Event feed(uint8_t byte, uint32_t nowMs) {
    Event result = Event::None;
    if (frameReady_) {
      length_ = 0;
      frameReady_ = false;
    }
    if (partialPending() && nowMs - lastByteAt_ >= kGm65FrameIdleTimeoutMs) {
      reset();
      result = Event::Dropped;
    }
    lastByteAt_ = nowMs;

    if (byte == '\r' || byte == '\n') {
      if (!partialPending()) return result;
      if (corrupt_ || discarding_) {
        reset();
        return Event::Dropped;
      }
      frameReady_ = true;
      return Event::Frame;
    }
    if (corrupt_ || discarding_) return result;
    if (!gm65IsPayloadByte(byte)) {
      length_ = 0;
      corrupt_ = true;
      return result;
    }
    if (length_ == kGm65MaxFrameBytes) {
      length_ = 0;
      discarding_ = true;
      return result;
    }
    buffer_[length_++] = static_cast<char>(byte);
    return result;
  }

  // Drops a partial frame that has been idle for kGm65FrameIdleTimeoutMs. Call this
  // from the polling loop, so a stalled frame is discarded even when no byte arrives.
  Event expire(uint32_t nowMs) {
    if (!partialPending() || nowMs - lastByteAt_ < kGm65FrameIdleTimeoutMs) return Event::None;
    reset();
    return Event::Dropped;
  }

  // The bytes of the frame reported by the last Event::Frame. Valid until the next feed().
  const char* frame() const { return buffer_; }
  std::size_t frameLength() const { return frameReady_ ? length_ : 0U; }

  // Discards everything held, for example when the scanner is reset.
  void reset() {
    length_ = 0;
    corrupt_ = false;
    discarding_ = false;
    frameReady_ = false;
  }

 private:
  bool partialPending() const { return !frameReady_ && (length_ > 0U || corrupt_ || discarding_); }

  char buffer_[kGm65MaxFrameBytes]{};
  std::size_t length_ = 0;
  uint32_t lastByteAt_ = 0;
  bool corrupt_ = false;
  bool discarding_ = false;
  bool frameReady_ = false;
};

}  // namespace inventatory_scan
