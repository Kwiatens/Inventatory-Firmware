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
// After a wake the module restarts before it answers. This wait is a first guess, not yet measured on a board: a boot
// that logs "GM65 profile not acknowledged" means it is too short.
constexpr unsigned long kModuleRestartMs = 500U;
constexpr size_t kZoneWriteAckLength = 7U;
constexpr uint8_t kZoneWriteAck[kZoneWriteAckLength] = {0x02, 0x00, 0x00, 0x01, 0x00, 0x33, 0x31};

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
    {0x0060, 0x21},  // Raw serial output; bits 6-5 = 01 select CRLF and bit 0 allows the tail (GM65 manual)
};

}  // namespace

bool Gm65Scanner::begin(uint32_t baudRate) {
  baudRate_ = baudRate;
  return start(true);
}

bool Gm65Scanner::start(bool wakeFirst) {
  frames_.reset();
  openPort();
  delay(100);
  if (wakeFirst) {
    wakeModule();
    restoreLamps();
  }
  active_ = applyBootProfile();
  profileApplied_ = active_;
  flushInput();
  acceptAfterMs_ = millis() + kStartupSettleMs;
  return active_;
}

void Gm65Scanner::openPort() {
  serial_.setRxBufferSize(1024);
  serial_.begin(baudRate_, SERIAL_8N1, GM65_RX_PIN, GM65_TX_PIN);
  serial_.setTimeout(5);
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

// A wake from standby only reopens the port. The module stays powered through standby and keeps the profile it has,
// so sending the profile again (25 acknowledged writes) would keep the screen dark for seconds. A profile that was
// never applied is still sent in full.
bool Gm65Scanner::resume() {
  if (active_) {
    return true;
  }
  if (!profileApplied_) {
    return start(false);
  }
  openPort();
  flushInput();
  acceptAfterMs_ = millis() + kStartupSettleMs;
  active_ = true;
  return true;
}

// Lamps off, then deep sleep. The module wakes only when a byte arrives on its UART, and the chip's next boot sends
// that byte (see wakeModule). Nothing here depends on the module's replies: a missing reply is logged, and the
// power-off goes on.
void Gm65Scanner::deepSleep() {
  if (!active_) {
    // Standby and a boot refusal both leave the port closed, and the module may be asleep from the last power-off.
    // A byte wakes it, so it is woken and given time to restart before it hears the commands.
    openPort();
    wakeModule();
  }
  uint8_t lamps = 0;
  if (!readZoneBit(kGm65LampZone, lamps)) {
    Serial.println("GM65 lamp read not acknowledged");
  } else if (!writeZoneBit(kGm65LampZone, gm65LampsOffZoneValue(lamps))) {
    Serial.println("GM65 lamps off not acknowledged");
  }
  if (!writeZoneByte(0x08, kGm65FunctionZone, kGm65DeepSleep, kZoneWriteAckTimeoutMs)) {
    Serial.println("GM65 deep sleep not acknowledged");
  }
  end();
}

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

  uint8_t command[kGm65ZoneCommandLength];
  gm65ZoneCommand(type, address, value, command);
  if (serial_.write(command, sizeof(command)) != sizeof(command)) {
    return false;
  }
  serial_.flush();

  uint8_t reply[kZoneWriteAckLength];
  const size_t replyLen = readReply(reply, kZoneWriteAckLength, ackTimeoutMs);

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

bool Gm65Scanner::readZoneBit(uint16_t address, uint8_t& value) {
  drainRx();

  uint8_t command[kGm65ZoneCommandLength];
  gm65ZoneCommand(0x07, address, 0x01, command);
  if (serial_.write(command, sizeof(command)) != sizeof(command)) {
    return false;
  }
  serial_.flush();

  uint8_t reply[kGm65ZoneReplyLength];
  if (readReply(reply, kGm65ZoneReplyLength, kZoneWriteAckTimeoutMs) != kGm65ZoneReplyLength) {
    return false;
  }
  return gm65ZoneReadValue(reply, value);
}

size_t Gm65Scanner::readReply(uint8_t* reply, size_t length, unsigned long timeoutMs) {
  size_t replyLen = 0;
  const unsigned long startedAt = millis();
  while (replyLen < length && millis() - startedAt <= timeoutMs) {
    if (serial_.available() > 0) {
      reply[replyLen++] = serial_.read();
    } else {
      delay(1);
    }
  }
  return replyLen;
}

// The module wakes on a byte from the chip and then restarts. It does not answer while it sleeps, so the restart is
// waited for instead of a reply.
void Gm65Scanner::wakeModule() {
  uint8_t command[kGm65ZoneCommandLength];
  gm65ZoneCommand(0x08, kGm65FunctionZone, kGm65Wake, command);
  serial_.write(command, sizeof(command));
  serial_.flush();
  delay(kModuleRestartMs);
}

// A module that did not restart after a deep sleep still holds the lamps off in RAM, and the profile save would write
// that into flash. The lamps are switched back on before the save. The firmware never sets both lamps off on purpose.
void Gm65Scanner::restoreLamps() {
  uint8_t lamps = 0;
  if (readZoneBit(kGm65LampZone, lamps) && gm65LampsAreOff(lamps)) {
    writeZoneBit(kGm65LampZone, gm65LampsStandardZoneValue(lamps));
  }
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
