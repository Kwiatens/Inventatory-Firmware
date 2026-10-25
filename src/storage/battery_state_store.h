// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <cstdint>

#include "InventatoryScanBattery.h"

namespace inventatory_scan {

// Keeps the gauge state across power-off in its own NVS namespace. An out-of-range stored value is
// rejected, not clamped; the gauge then starts from the cell voltage. The first layout's charge (rem_uah) is read
// once and replaced at the next save.
class BatteryStateStore {
 public:
  bool begin();
  bool load(BatteryGaugeState& state) const;
  // Writes only values changed since the last save or load, so unchanged state wears no flash.
  bool save(const BatteryGaugeState& state);

 private:
  // What NVS holds as of the last load or save. Load updates it, so it is mutable.
  mutable Preferences prefs_;
  mutable BatteryGaugeState last_;
  mutable bool haveLast_ = false;
  // Loaded from the first layout: the next save writes every key and removes the old ones.
  mutable bool legacy_ = false;
};

}  // namespace inventatory_scan
