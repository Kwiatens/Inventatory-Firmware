// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <Arduino.h>

namespace inventatory_scan::hardware {

// Waveshare ESP32-S3-Nano header labels in the comments. Software SPI, so any GPIO works.
constexpr uint8_t kLcdClockPin = 13;  // A6
constexpr uint8_t kLcdDcPin = 11;     // A4
constexpr uint8_t kLcdResetPin = 12;  // A5
constexpr uint8_t kLcdCsPin = 21;     // D10
constexpr uint8_t kLcdDataPin = 14;   // A7
constexpr uint8_t kLcdContrast = 45;
// ST7565 start-line commands can be corrupted by noise on the SPI harness. Reassert the
// top-of-frame mapping regularly without sending the complete framebuffer again.
constexpr unsigned long kLcdStartLineRecoveryIntervalMs = 250UL;
constexpr uint8_t kLcdStartLineRecoveryAttempts = 3;

}  // namespace inventatory_scan::hardware
