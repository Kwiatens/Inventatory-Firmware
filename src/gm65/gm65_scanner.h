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
  bool poll(String& code);
  void flushInput();
  bool active() const;

 private:
  // Opens the UART, applies the boot profile and starts accepting frames after a settle time.
  bool start();
  bool applyBootProfile();
  bool writeZoneBit(uint16_t address, uint8_t value);
  bool writeZoneByte(uint8_t type, uint16_t address, uint8_t value);
  void drainRx();

  HardwareSerial serial_{1};
  Gm65FrameAssembler frames_;
  uint32_t baudRate_ = 9600;
  bool active_ = false;
  unsigned long acceptAfterMs_ = 0;
};

}  // namespace inventatory_scan
