// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#include "power/battery_monitor.h"

#include "config/config.h"
#include "config/hardware.h"

namespace inventatory_scan {

namespace {

constexpr uint8_t kSamples = 8;

// ADC1 (GPIO1-10) keeps reading while Wi-Fi is active; ADC2 does not.
static_assert(BATTERY_ADC_PIN >= 1 && BATTERY_ADC_PIN <= 10, "BATTERY_ADC_PIN must be an ADC1 pin (GPIO1-10)");
static_assert(BATTERY_DIVIDER_BOTTOM_OHMS > 0UL, "BATTERY_DIVIDER_BOTTOM_OHMS must be non-zero");

}  // namespace

void BatteryMonitor::begin() {
  // 12 dB attenuation covers the divider output for a LiPo (about 2.1 V at 4.2 V with equal resistors).
  analogSetPinAttenuation(BATTERY_ADC_PIN, ADC_11db);
  // The pull-downs read an unwired or floating LED pad as "not lit".
  pinMode(hardware::kChargingLedPin, INPUT_PULLDOWN);
  pinMode(hardware::kChargeDoneLedPin, INPUT_PULLDOWN);
}

uint32_t BatteryMonitor::readMillivolts() const {
  uint32_t pinSum = 0;
  for (uint8_t sample = 0; sample < kSamples; ++sample) {
    pinSum += analogReadMilliVolts(BATTERY_ADC_PIN);
  }
  const uint64_t pinMillivolts = pinSum / kSamples;
  // The pin sees battery * bottom / (top + bottom).
  return static_cast<uint32_t>(pinMillivolts * (BATTERY_DIVIDER_TOP_OHMS + BATTERY_DIVIDER_BOTTOM_OHMS) /
                               BATTERY_DIVIDER_BOTTOM_OHMS);
}

ChargeState BatteryMonitor::chargeState() const {
  const bool redLit = digitalRead(hardware::kChargingLedPin) == hardware::kChargeLedOnLevel;
  const bool greenLit = digitalRead(hardware::kChargeDoneLedPin) == hardware::kChargeLedOnLevel;
  return chargeStateFromLeds(redLit, greenLit);
}

}  // namespace inventatory_scan
