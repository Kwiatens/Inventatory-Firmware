#pragma once

#include <Arduino.h>

namespace inventatory_scan::hardware {

constexpr uint8_t kLcdClockPin = 9;
constexpr uint8_t kLcdDcPin = 10;
constexpr uint8_t kLcdResetPin = 11;
constexpr uint8_t kLcdCsPin = 12;
constexpr uint8_t kLcdDataPin = 13;
constexpr uint8_t kLcdContrast = 45;
// ST7565 start-line commands can be corrupted by noise on the SPI harness. Reassert the
// top-of-frame mapping regularly without sending the complete framebuffer again.
constexpr unsigned long kLcdStartLineRecoveryIntervalMs = 250UL;
constexpr uint8_t kLcdStartLineRecoveryAttempts = 3;

}  // namespace inventatory_scan::hardware
