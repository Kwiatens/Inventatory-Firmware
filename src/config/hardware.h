// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <Arduino.h>

namespace inventatory_scan::hardware {

// Trailing labels are Nano header pins. Software SPI, so any GPIO works.
constexpr uint8_t kLcdClockPin = 13;  // A6
constexpr uint8_t kLcdDcPin = 11;     // A4
constexpr uint8_t kLcdResetPin = 12;  // A5
constexpr uint8_t kLcdCsPin = 21;     // D10
constexpr uint8_t kLcdDataPin = 14;   // A7
constexpr uint8_t kLcdContrast = 45;
// SPI noise can corrupt ST7565 start-line commands. Reassert top-of-frame regularly,
// without a full resend.
constexpr unsigned long kLcdStartLineRecoveryIntervalMs = 250UL;
constexpr uint8_t kLcdStartLineRecoveryAttempts = 3;

// Backlight through an external transistor; the board has no driver. Header MOSI pad, right pin 14.
// Needs a gate pull-down so it stays off while the chip sleeps. Moved from GPIO2, which now reads
// the charger input.
constexpr uint8_t kBacklightPin = 38;
constexpr uint8_t kBacklightOnLevel = 1;
constexpr uint8_t kBacklightOffLevel = kBacklightOnLevel ? 0 : 1;

// Charging signal from the UPS module, lit while the cell charges. Through a 1 MOhm / 1 MOhm
// divider to pad A3 (GPIO4, ADC1 channel 3). Read with the ADC: a digital input reads 2.2 V as low.
// No pull resistor: a pull-down pulled the signal to 0 V.
constexpr uint8_t kChargingLedPin = 4;

// Charger input sense: midpoint of a 1 MOhm / 1 MOhm divider on the 5 V input, read with the ADC.
// About 2.2 V plugged in (input about 4.4 V), 0 V unplugged. No pull resistor: a pull-down would
// pull the midpoint low. Pad A1 (GPIO2, ADC1 channel 1).
constexpr uint8_t kConnectorSensePin = 2;

// Gain in ppm on the calculated cell voltage, from one point: 3553 mV read for 3696 mV.
// Check a second point, a docked cell near 4.1 V, before trusting the top of the curve.
constexpr uint32_t kBatteryReadingScalePpm = 1040248UL;

}  // namespace inventatory_scan::hardware
