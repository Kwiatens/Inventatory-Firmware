// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <Arduino.h>

#include "InventatoryScanBattery.h"

namespace inventatory_scan {

// LiPo voltage read through the resistor divider on BATTERY_ADC_PIN (see config.example.h),
// and whether the charger is charging, from its LED signal on hardware::kChargingLedPin. Both are read with
// the ADC, through 1 MOhm / 1 MOhm dividers.
class BatteryMonitor {
 public:
  void begin();
  // Battery terminal voltage in millivolts, averaged over several calibrated ADC conversions.
  uint32_t readMillivolts() const;
  // The charger LED signal's midpoint in millivolts (hardware::kChargingLedPin), averaged over several
  // conversions. About 2200 while lit, 0 when dark.
  uint32_t chargingLedMillivolts() const;
  // The charger input's midpoint in millivolts (hardware::kConnectorSensePin), averaged over several
  // conversions. About 2500 plugged in, 0 unplugged.
  uint32_t connectorMillivolts() const;
};

}  // namespace inventatory_scan
