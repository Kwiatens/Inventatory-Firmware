#pragma once

#include <Arduino.h>
#include <cstddef>

namespace hims_scan {

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
  bool applyBootProfile();
  bool writeZoneBit(uint16_t address, uint8_t value);
  bool writeZoneByte(uint8_t type, uint16_t address, uint8_t value);
  static uint16_t crcCcitt(const uint8_t* bytes, size_t length);

  HardwareSerial serial_{1};
  String buffer_;
  unsigned long lastByteAt_ = 0;
  uint32_t baudRate_ = 9600;
  bool active_ = false;
  bool discardingOversized_ = false;
};

}  // namespace hims_scan
