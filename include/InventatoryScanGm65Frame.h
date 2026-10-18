// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Inventatory Scan R1 GM65 frame assembly, zone-bit commands and their CRC. Pure logic with no
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

// Zone bit 0x0000 holds the head lamp (bits 5-4) and the pointing light (bits 3-2). 00 is off and 01 is standard.
constexpr uint16_t kGm65LampZone = 0x0000U;
constexpr uint8_t kGm65LampBits = 0x3CU;
constexpr uint8_t kGm65LampsStandard = 0x14U;
// Zone bit 0x0000 bits 1-0 set the read mode: 10 is continuous, the mode the scanner reads in, and 00 is manual.
constexpr uint8_t kGm65ReadModeBits = 0x03U;
constexpr uint8_t kGm65ReadModeManual = 0x00U;
// Zone bit 0x00D9 is the function zone. Writing 0xA5 puts the module into deep sleep, and writing 0x00 wakes it.
// The module restarts when it wakes, so its zone bits come back from flash.
constexpr uint16_t kGm65FunctionZone = 0x00D9U;
constexpr uint8_t kGm65DeepSleep = 0xA5U;
constexpr uint8_t kGm65Wake = 0x00U;

// A zone-bit command is 7E 00, type, length 01, address (2 bytes), value, CRC-16 (2 bytes).
constexpr std::size_t kGm65ZoneCommandLength = 9U;
// A zone-bit reply is 02 00 00 01 <value> <CRC-16 (2 bytes)>, and a write is acknowledged with value 00.
constexpr std::size_t kGm65ZoneReplyLength = 7U;

// Fills a zone-bit command. Type 07 reads one zone bit, 08 writes it and 09 saves the zone bits to flash.
// The CRC covers the bytes after the 7E 00 header.
inline void gm65ZoneCommand(uint8_t type, uint16_t address, uint8_t value, uint8_t* out) {
  out[0] = 0x7EU;
  out[1] = 0x00U;
  out[2] = type;
  out[3] = 0x01U;
  out[4] = static_cast<uint8_t>(address >> 8U);
  out[5] = static_cast<uint8_t>(address & 0xFFU);
  out[6] = value;
  const uint16_t crc = gm65Crc16(out + 2, 5U);
  out[7] = static_cast<uint8_t>(crc >> 8U);
  out[8] = static_cast<uint8_t>(crc & 0xFFU);
}

// Reads the value from a zone read reply. The reply is refused unless its header and CRC match.
inline bool gm65ZoneReadValue(const uint8_t* reply, uint8_t& value) {
  if (reply[0] != 0x02U || reply[1] != 0x00U || reply[2] != 0x00U || reply[3] != 0x01U) return false;
  const uint16_t crc = gm65Crc16(reply + 2, 3U);
  if (reply[5] != static_cast<uint8_t>(crc >> 8U) || reply[6] != static_cast<uint8_t>(crc & 0xFFU)) return false;
  value = reply[4];
  return true;
}

// True only when both lamps are 00. A lamp set to any other value counts as on, and is not restored.
inline bool gm65LampsAreOff(uint8_t zoneValue) { return (zoneValue & kGm65LampBits) == 0U; }

// The zone value with both lamps off. The other bits are kept, including the scan mode in bits 1-0.
inline uint8_t gm65LampsOffZoneValue(uint8_t zoneValue) {
  return static_cast<uint8_t>(zoneValue & ~kGm65LampBits);
}

// The zone value with both lamps on standard, which is the module's default.
inline uint8_t gm65LampsStandardZoneValue(uint8_t zoneValue) {
  return static_cast<uint8_t>((zoneValue & ~kGm65LampBits) | kGm65LampsStandard);
}

// The zone value for deep sleep: both lamps off and manual read mode, so the module stops reading. The other bits are
// kept. The boot profile writes continuous mode again, and the save never stores the manual mode.
inline uint8_t gm65SleepZoneValue(uint8_t zoneValue) {
  return static_cast<uint8_t>((zoneValue & ~(kGm65LampBits | kGm65ReadModeBits)) | kGm65ReadModeManual);
}

// Printable ASCII, plus the group and record separators that ISO 15434 messages use. Any
// other byte marks the frame as corrupt, except a terminator (see gm65IsFrameTerminator).
inline bool gm65IsPayloadByte(uint8_t byte) {
  return (byte >= 0x20U && byte <= 0x7EU) || byte == 0x1DU || byte == 0x1EU;
}

// A frame ends at a carriage return, a line feed, or the end-of-transmission byte that closes
// an ISO 15434 message. The GM65 sends a Data Matrix with no CR or LF after its RS EOT trailer,
// so the EOT is what ends that scan.
inline bool gm65IsFrameTerminator(uint8_t byte) {
  return byte == '\r' || byte == '\n' || byte == 0x04U;
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

    if (gm65IsFrameTerminator(byte)) {
      if (!partialPending()) return result;
      if (corrupt_ || discarding_ || !holdsPrintable()) {
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

  // A frame made only of separators is not a scan. A captured Data Matrix ends in RS EOT RS EOT,
  // and the second pair must not be reported as a scan of its own.
  bool holdsPrintable() const {
    for (std::size_t index = 0; index < length_; ++index) {
      const auto byte = static_cast<uint8_t>(buffer_[index]);
      if (byte != 0x1DU && byte != 0x1EU) return true;
    }
    return false;
  }

  char buffer_[kGm65MaxFrameBytes]{};
  std::size_t length_ = 0;
  uint32_t lastByteAt_ = 0;
  bool corrupt_ = false;
  bool discarding_ = false;
  bool frameReady_ = false;
};

}  // namespace inventatory_scan
