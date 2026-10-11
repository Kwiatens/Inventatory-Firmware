// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <Arduino.h>

namespace inventatory_scan {

// LiPo voltage read through the resistor divider on BATTERY_ADC_PIN (see config.example.h).
class BatteryMonitor {
 public:
  void begin();
  // Battery terminal voltage in millivolts, averaged over several calibrated ADC conversions.
  uint32_t readMillivolts() const;
};

}  // namespace inventatory_scan
