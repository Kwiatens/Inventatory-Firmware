// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <Arduino.h>

#include "InventatoryScanBattery.h"

namespace inventatory_scan {

// LiPo voltage read through the resistor divider on BATTERY_ADC_PIN (see config.example.h),
// and the charger state from the two status LEDs on hardware::kChargingLedPin and
// hardware::kChargeDoneLedPin.
class BatteryMonitor {
 public:
  void begin();
  // Battery terminal voltage in millivolts, averaged over several calibrated ADC conversions.
  uint32_t readMillivolts() const;
  // Charging, done, or unplugged, from the status LEDs.
  ChargeState chargeState() const;
  // True while a charger is connected: either LED is lit.
  bool chargerPresent() const;
};

}  // namespace inventatory_scan
