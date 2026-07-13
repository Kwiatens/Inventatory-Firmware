#pragma once

#include <Arduino.h>

namespace hims_scan::hardware {

constexpr uint8_t kLcdClockPin = 9;
constexpr uint8_t kLcdDcPin = 10;
constexpr uint8_t kLcdResetPin = 11;
constexpr uint8_t kLcdCsPin = 12;
constexpr uint8_t kLcdDataPin = 13;
constexpr uint8_t kLcdContrast = 45;

}  // namespace hims_scan::hardware
