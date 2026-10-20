// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <Arduino.h>

#include "InventatoryScanBattery.h"

namespace inventatory_scan {

// LiPo voltage on BATTERY_ADC_PIN (see config.example.h), and the charger LED on
// hardware::kChargingLedPin. Both are read with the ADC through resistor dividers.
class BatteryMonitor {
 public:
  void begin();
  // Battery voltage in millivolts, averaged over several calibrated conversions.
  uint32_t readMillivolts() const;
  // Charger LED midpoint in millivolts, averaged. About 2200 lit, 0 dark.
  uint32_t chargingLedMillivolts() const;
  // Charger input midpoint in millivolts (hardware::kConnectorSensePin), averaged.
  // About 2500 plugged in, 0 unplugged.
  uint32_t connectorMillivolts() const;
};

}  // namespace inventatory_scan
