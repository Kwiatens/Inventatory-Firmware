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

// LCD backlight: one GPIO switches the LED through an external transistor; the board has no
// backlight driver. The pin is header pad A1 (GPIO2, left header pin 5 in the Waveshare
// ESP32-S3-Nano schematic). No other net on the board uses it. Add a gate pull-down so the
// backlight stays off while the chip sleeps.
constexpr uint8_t kBacklightPin = 2;
constexpr uint8_t kBacklightOnLevel = 1;
constexpr uint8_t kBacklightOffLevel = kBacklightOnLevel ? 0 : 1;

// Charger status LEDs from the LiPo UPS module: red lit while the cell charges, green lit when
// charging is complete. Both are inputs read against GND. Use a divider or level shifter if a
// pad swings above 3.3 V. An internal pull-down reads an unwired pad as "not lit".
// Red is header pad A3 (GPIO4, left header pin 4). Green is header pad MOSI (GPIO38, right header
// pin 14), which replaced the old dock-present line.
constexpr uint8_t kChargingLedPin = 4;
constexpr uint8_t kChargeDoneLedPin = 38;
// Level that means "LED lit". Change to 0 only after measuring the pad with the LED on.
constexpr uint8_t kChargeLedOnLevel = 1;

}  // namespace inventatory_scan::hardware
