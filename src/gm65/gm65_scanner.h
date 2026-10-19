// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "InventatoryScanGm65Frame.h"

#include <Arduino.h>
#include <cstddef>

namespace inventatory_scan {

class Gm65Scanner {
 public:
  bool begin(uint32_t baudRate = 9600);
  void end();
  bool suspend();
  bool resume();
  // Lamps off and module in deep sleep. Call just before the chip sleeps.
  void deepSleep();
  bool poll(String& code);
  void flushInput();
  bool active() const;

 private:
  // Opens the UART, wakes the module (it may be in deep sleep after a chip power-off), and applies
  // the boot profile. False if it does not wake or the profile is not acknowledged.
  bool start();
  void openPort();
  // Returns false when the module does not answer after the wake.
  bool wakeModule();
  void logZones(const char* label);
  bool applyBootProfile();
  bool writeZoneBit(uint16_t address, uint8_t value);
  bool writeZoneByte(uint8_t type, uint16_t address, uint8_t value, unsigned long ackTimeoutMs);
  bool readZoneBit(uint16_t address, uint8_t& value);
  size_t readReply(uint8_t* reply, size_t length, unsigned long timeoutMs);
  void drainRx();

  HardwareSerial serial_{1};
  Gm65FrameAssembler frames_;
  uint32_t baudRate_ = 9600;
  bool active_ = false;
  // Boot profile acknowledged and saved; the module keeps it while powered.
  bool profileApplied_ = false;
  unsigned long acceptAfterMs_ = 0;
};

}  // namespace inventatory_scan
