// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Inventatory Scan R1 device preferences kept independently from Wi-Fi provisioning.
#pragma once

#include <Arduino.h>
#include <Preferences.h>

namespace inventatory_scan {

class DeviceSettingsStore {
 public:
  bool begin();
  uint8_t contrast() const;
  bool saveContrast(uint8_t contrast);
  // Stored BacklightMode value; the default when nothing valid is stored.
  uint8_t backlightMode() const;
  bool saveBacklightMode(uint8_t mode);
  // Energy saving is off when nothing is stored.
  bool energySavingEnabled() const;
  bool saveEnergySavingEnabled(bool enabled);
  // Stored SleepDelay and PowerOffDelay values; the defaults when nothing valid is stored.
  uint8_t sleepDelay() const;
  bool saveSleepDelay(uint8_t delay);
  uint8_t powerOffDelay() const;
  bool savePowerOffDelay(uint8_t delay);

 private:
  mutable Preferences prefs_;
  // Last contrast known to be stored in NVS (or the effective default after begin()).
  // -1 means unknown, so the next save always writes.
  int16_t lastContrast_ = -1;
  int16_t lastBacklightMode_ = -1;
  int16_t lastEnergySaving_ = -1;
  int16_t lastSleepDelay_ = -1;
  int16_t lastPowerOffDelay_ = -1;
};

}  // namespace inventatory_scan
