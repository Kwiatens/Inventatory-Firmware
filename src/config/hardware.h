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
// backlight driver. The pin is header pad MOSI (GPIO38, right header pin 14). It moved from GPIO2 so
// that GPIO2 can read the charger input (kConnectorSensePin). Add a gate pull-down so the
// backlight stays off while the chip sleeps.
constexpr uint8_t kBacklightPin = 38;
constexpr uint8_t kBacklightOnLevel = 1;
constexpr uint8_t kBacklightOffLevel = kBacklightOnLevel ? 0 : 1;

// Charger LED signal from the LiPo UPS module: the charging signal, lit while the cell charges. It goes
// through a 1 MOhm / 1 MOhm divider to header pad A3 (GPIO4, ADC1 channel 3, left header pin 4). It is read
// with the ADC, not as a digital input: the midpoint is about 2.2 V when lit and about 0 V when dark, which
// a digital input reads as low. The pin has no pull resistor, because a pull-down pulled the signal to 0 V.
constexpr uint8_t kChargingLedPin = 4;

// Charger input sense: the midpoint of a 1 MOhm / 1 MOhm divider on the charger's 5 V input, read with the
// ADC. About 2.2 V while the charger is plugged in (the input is about 4.4 V), 0 V when it is not. It has no pull resistor, because an
// internal pull-down would pull the midpoint down. Header pad A1 (GPIO2, ADC1 channel 1, left header pin 5).
constexpr uint8_t kConnectorSensePin = 2;

// Battery reading gain, in parts per million, applied to the calculated cell voltage. Measured on this
// board: the firmware read 3553 mV for a 3696 mV cell, so the reading was 3.9% low. This corrects that one
// point. Check a second point, a docked cell near 4.1 V, before trusting the top of the curve.
constexpr uint32_t kBatteryReadingScalePpm = 1040248UL;

}  // namespace inventatory_scan::hardware
