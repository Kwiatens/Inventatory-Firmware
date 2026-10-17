// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Inventatory Scan R1 GM65 scanner driver.

#include "gm65/gm65_scanner.h"

#include "config/config.h"

#include <cstddef>

namespace inventatory_scan {

namespace {

constexpr unsigned long kStartupSettleMs = 250U;
// Reply to a zone write: exactly kZoneWriteAckLength bytes, within the timeout. The module takes up to about 190 ms
// to reply when writes follow each other closely (measured on a GM65 clone). A 150 ms wait missed some replies, and
// each late reply was then read as the answer to the next write.
constexpr unsigned long kZoneWriteAckTimeoutMs = 500U;
// The save command writes the module's flash, and the module answers it after about 340 ms (measured on a GM65
// clone). A 150 ms wait missed that reply, so the whole profile was reported as not acknowledged.
constexpr unsigned long kSaveAckTimeoutMs = 1000U;
constexpr unsigned long kZoneWriteAckSettleMs = 20U;
constexpr size_t kZoneWriteAckLength = 7U;
constexpr uint8_t kZoneWriteAck[kZoneWriteAckLength] = {0x02, 0x00, 0x00, 0x01, 0x00, 0x33, 0x31};

struct ZoneWrite {
  uint16_t address;
  uint8_t value;
};

constexpr ZoneWrite kBootProfile[] = {
    {0x0005, 0x05},  // Read interval 0.5 s between reads (manual default 1.0 s)
    {0x0006, 0xFF},  // Single-read window 25.5 s before the module stops trying (manual default 5 s)
    {0x000D, 0x00},  // Serial port output, no virtual keyboard
    {0x000E, 0x04},  // Decode success beep on, HID protocol features off
    {0x0013, 0x94},  // Same barcode not read again within 2.0 s of its last read
    {0x002C, 0x02},  // Full width read area, all barcode types allowed
    {0x002E, 0x00},  // EAN13 off
    {0x002F, 0x00},  // EAN8 off
    {0x0030, 0x00},  // UPCA off
    {0x0031, 0x00},  // UPCE0 off
    {0x0032, 0x00},  // UPCE1 off
    {0x0033, 0x00},  // Code128 off (GM65 manual: 0 forbids, 1 allows)
    {0x0036, 0x00},  // Code39 off
    {0x0039, 0x00},  // Code93 off
    {0x003C, 0x00},  // Codabar off
    {0x003F, 0x01},  // QR on
    {0x0040, 0x00},  // Interleaved 2 of 5 off
    {0x0043, 0x00},  // Industrial 25 off
    {0x0046, 0x00},  // Matrix 2 of 5 off
    {0x0049, 0x00},  // Code11 off
    {0x004C, 0x00},  // MSI off
    {0x004F, 0x00},  // RSS-14 off
    {0x0050, 0x00},  // RSS limited off
    {0x0051, 0x00},  // RSS expanded off
    {0x0054, 0x01},  // Data Matrix on
    {0x0055, 0x00},  // PDF417 off
    {0x0060, 0x21},  // Raw serial output; bits 6-5 = 01 select CRLF and bit 0 allows the tail (GM65 manual)
};

}  // namespace

bool Gm65Scanner::begin(uint32_t baudRate) {
  baudRate_ = baudRate;
  return start();
}

bool Gm65Scanner::start() {
  frames_.reset();
  serial_.setRxBufferSize(1024);
  serial_.begin(baudRate_, SERIAL_8N1, GM65_RX_PIN, GM65_TX_PIN);
  serial_.setTimeout(5);
  delay(100);
  active_ = applyBootProfile();
  flushInput();
  acceptAfterMs_ = millis() + kStartupSettleMs;
  return active_;
}

void Gm65Scanner::end() {
  serial_.end();
  active_ = false;
  frames_.reset();
  acceptAfterMs_ = 0;
}

bool Gm65Scanner::suspend() {
  if (!active_) {
    return true;
  }
  end();
  return true;
}

bool Gm65Scanner::resume() { return active_ || start(); }

void Gm65Scanner::flushInput() {
  drainRx();
  frames_.reset();
}

bool Gm65Scanner::active() const {
  return active_;
}

void Gm65Scanner::drainRx() {
  while (serial_.available() > 0) {
    serial_.read();
  }
}

bool Gm65Scanner::writeZoneByte(uint8_t type, uint16_t address, uint8_t value, unsigned long ackTimeoutMs) {
  drainRx();

  const uint8_t frame[] = {
      0x7E,
      0x00,
      type,
      0x01,
      static_cast<uint8_t>((address >> 8U) & 0xFFU),
      static_cast<uint8_t>(address & 0xFFU),
      value,
  };
  const uint16_t crc = gm65Crc16(frame + 2, sizeof(frame) - 2U);
  const uint8_t trailer[] = {
      static_cast<uint8_t>((crc >> 8U) & 0xFFU),
      static_cast<uint8_t>(crc & 0xFFU),
  };

  if (serial_.write(frame, sizeof(frame)) != sizeof(frame)) {
    return false;
  }
  if (serial_.write(trailer, sizeof(trailer)) != sizeof(trailer)) {
    return false;
  }
  serial_.flush();

  uint8_t reply[kZoneWriteAckLength];
  size_t replyLen = 0;
  unsigned long start = millis();
  while (replyLen < kZoneWriteAckLength && millis() - start <= ackTimeoutMs) {
    if (serial_.available() > 0) {
      reply[replyLen++] = serial_.read();
    } else {
      delay(1);
    }
  }

  delay(kZoneWriteAckSettleMs);

  if (replyLen != kZoneWriteAckLength) {
    return false;
  }

  for (size_t i = 0; i < kZoneWriteAckLength; ++i) {
    if (reply[i] != kZoneWriteAck[i]) {
      return false;
    }
  }

  return true;
}

bool Gm65Scanner::writeZoneBit(uint16_t address, uint8_t value) {
  return writeZoneByte(0x08, address, value, kZoneWriteAckTimeoutMs);
}

bool Gm65Scanner::applyBootProfile() {
  for (int attempt = 1; attempt <= 3; ++attempt) {
    bool attemptOk = true;
    for (const auto& entry : kBootProfile) {
      if (!writeZoneBit(entry.address, entry.value)) {
        attemptOk = false;
        break;
      }
    }
    if (attemptOk && writeZoneByte(0x09, 0x0000, 0x00, kSaveAckTimeoutMs)) {
      serial_.flush();
      delay(50);
      return true;
    }
    Serial.println("GM65 profile not acknowledged");
    delay(50);
  }
  return false;
}

bool Gm65Scanner::poll(String& code) {
  code.clear();
  while (serial_.available() > 0) {
    const int raw = serial_.read();
    if (raw < 0) {
      break;
    }
    const unsigned long now = millis();
    if (static_cast<long>(now - acceptAfterMs_) < 0) {
      frames_.reset();
      continue;
    }
    // A frame is reported only when its CR, LF or end-of-transmission terminator arrives. Partial,
    // corrupt and oversized frames are dropped, and nothing is ever reported as a scan without its
    // terminator (see InventatoryScanGm65Frame.h).
    if (frames_.feed(static_cast<uint8_t>(raw), now) == Gm65FrameAssembler::Event::Frame) {
      code.reserve(frames_.frameLength());
      for (std::size_t index = 0; index < frames_.frameLength(); ++index) code += frames_.frame()[index];
      return true;
    }
  }
  // A frame that stopped mid-way is dropped even when no further byte arrives.
  frames_.expire(millis());
  return false;
}

}  // namespace inventatory_scan
