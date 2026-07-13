// HIMS Scan R1 GM65 scanner driver.

#include "gm65/gm65_scanner.h"

#include "config/config.h"

#include <cctype>
#include <cstddef>

namespace hims_scan {

namespace {

constexpr size_t kMaxScanLength = 1024U;

struct ZoneWrite {
  uint16_t address;
  uint8_t value;
};

constexpr ZoneWrite kBootProfile[] = {
    {0x000D, 0x00},  // Serial port output, no virtual keyboard
    {0x000E, 0x04},  // Decode success beep on, HID protocol features off
    {0x002C, 0x02},  // Full width read area, all barcode types allowed
    {0x002E, 0x00},  // EAN13 off
    {0x002F, 0x00},  // EAN8 off
    {0x0030, 0x00},  // UPCA off
    {0x0031, 0x00},  // UPCE0 off
    {0x0032, 0x00},  // UPCE1 off
    {0x0033, 0x00},  // Code128 on
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
    {0x0060, 0x20},  // Raw serial output with CRLF terminator
};

bool isPayloadCharacter(char ch) {
  const auto byte = static_cast<unsigned char>(ch);
  return std::isprint(byte) != 0 || byte == 0x1DU || byte == 0x1EU || byte == 0x04U;
}

}  // namespace

bool Gm65Scanner::begin(uint32_t baudRate) {
  baudRate_ = baudRate;
  buffer_.clear();
  lastByteAt_ = 0;
  discardingOversized_ = false;
  serial_.setRxBufferSize(1024);
  serial_.begin(baudRate_, SERIAL_8N1, GM65_RX_PIN, GM65_TX_PIN);
  serial_.setTimeout(5);
  delay(100);
  applyBootProfile();
  flushInput();
  active_ = true;
  return true;
}

void Gm65Scanner::end() {
  serial_.end();
  active_ = false;
  buffer_.clear();
  lastByteAt_ = 0;
  discardingOversized_ = false;
}

bool Gm65Scanner::suspend() {
  if (!active_) {
    return true;
  }
  end();
  return true;
}

bool Gm65Scanner::resume() {
  if (active_) {
    return true;
  }
  serial_.setRxBufferSize(1024);
  serial_.begin(baudRate_, SERIAL_8N1, GM65_RX_PIN, GM65_TX_PIN);
  serial_.setTimeout(5);
  delay(100);
  const bool ok = applyBootProfile();
  flushInput();
  active_ = true;
  return ok;
}

void Gm65Scanner::flushInput() {
  while (serial_.available() > 0) {
    serial_.read();
  }
  buffer_.clear();
  lastByteAt_ = 0;
  discardingOversized_ = false;
}

bool Gm65Scanner::active() const {
  return active_;
}

uint16_t Gm65Scanner::crcCcitt(const uint8_t* bytes, size_t length) {
  uint16_t crc = 0x0000;
  for (size_t i = 0; i < length; ++i) {
    crc ^= static_cast<uint16_t>(bytes[i]) << 8U;
    for (uint8_t bit = 0; bit < 8U; ++bit) {
      if ((crc & 0x8000U) != 0U) {
        crc = static_cast<uint16_t>((crc << 1U) ^ 0x1021U);
      } else {
        crc <<= 1U;
      }
    }
  }
  return crc;
}

bool Gm65Scanner::writeZoneByte(uint8_t type, uint16_t address, uint8_t value) {
  const uint8_t frame[] = {
      0x7E,
      0x00,
      type,
      0x01,
      static_cast<uint8_t>((address >> 8U) & 0xFFU),
      static_cast<uint8_t>(address & 0xFFU),
      value,
  };
  const uint16_t crc = crcCcitt(frame + 2, sizeof(frame) - 2U);
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
  delay(20);
  return true;
}

bool Gm65Scanner::writeZoneBit(uint16_t address, uint8_t value) {
  return writeZoneByte(0x08, address, value);
}

bool Gm65Scanner::applyBootProfile() {
  bool ok = true;
  for (const auto& entry : kBootProfile) {
    ok = writeZoneBit(entry.address, entry.value) && ok;
  }
  ok = writeZoneByte(0x09, 0x0000, 0x00) && ok;
  serial_.flush();
  delay(50);
  return ok;
}

bool Gm65Scanner::poll(String& code) {
  code.clear();
  while (serial_.available() > 0) {
    const int raw = serial_.read();
    if (raw < 0) {
      break;
    }
    const char ch = static_cast<char>(raw);
    if (ch == '\r' || ch == '\n') {
      if (discardingOversized_) {
        discardingOversized_ = false;
        buffer_.clear();
        lastByteAt_ = 0;
        continue;
      }
      if (buffer_.length() > 0) {
        code = buffer_;
        buffer_.clear();
        lastByteAt_ = 0;
        return true;
      }
      continue;
    }
    if (discardingOversized_) {
      lastByteAt_ = millis();
      continue;
    }
    if (isPayloadCharacter(ch)) {
      buffer_ += ch;
      lastByteAt_ = millis();
    }
    if (buffer_.length() > kMaxScanLength) {
      buffer_.clear();
      lastByteAt_ = millis();
      discardingOversized_ = true;
    }
  }

  // GM65 normally terminates scans with CR/LF. Also accept a completed burst
  // after a short idle gap so a delayed network pass or lost terminator cannot
  // leave a valid barcode stuck in the receive buffer forever.
  if (buffer_.length() > 0 && lastByteAt_ != 0 && millis() - lastByteAt_ >= 30U) {
    code = buffer_;
    buffer_.clear();
    lastByteAt_ = 0;
    return true;
  }
  if (discardingOversized_ && lastByteAt_ != 0 && millis() - lastByteAt_ >= 30U) {
    discardingOversized_ = false;
    lastByteAt_ = 0;
  }
  return false;
}

}  // namespace hims_scan
