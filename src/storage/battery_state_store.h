// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <cstdint>

#include "InventatoryScanBattery.h"

namespace inventatory_scan {

// Keeps the battery gauge's state across a power-off in its own NVS namespace. A stored value
// outside its range is rejected, not clamped: the gauge then starts from the cell voltage, as a
// first boot does.
class BatteryStateStore {
 public:
  bool begin();
  bool load(BatteryGaugeState& state) const;
  // Writes only the values that changed since the last save or load, so an unchanged state
  // wears no flash.
  bool save(const BatteryGaugeState& state);

 private:
  // What NVS holds, as the last load or save left it. Loading updates it, so it is mutable.
  mutable Preferences prefs_;
  mutable BatteryGaugeState last_;
  mutable bool haveLast_ = false;
};

}  // namespace inventatory_scan
