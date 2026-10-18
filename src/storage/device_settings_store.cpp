// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Inventatory Scan R1 device preferences kept independently from Wi-Fi provisioning.

#include "storage/device_settings_store.h"

#include "InventatoryScanUiPolicy.h"
#include "config/hardware.h"

namespace inventatory_scan {

namespace {
constexpr char kSettingsNamespace[] = "inv_device";
constexpr uint8_t kMaxContrast = 63;
}  // namespace

bool DeviceSettingsStore::begin() {
  if (!prefs_.begin(kSettingsNamespace, false)) return false;
  lastContrast_ = contrast();
  lastBacklightMode_ = backlightMode();
  lastEnergySaving_ = energySavingEnabled() ? 1 : 0;
  lastSleepDelay_ = sleepDelay();
  lastPowerOffDelay_ = powerOffDelay();
  lastDockMode_ = dockModeEnabled() ? 1 : 0;
  return true;
}

// Writes value under key unless NVS is already known to hold it, so a repeated save wears no flash.
bool DeviceSettingsStore::saveCached(const char* key, uint8_t value, int16_t& last) {
  if (last >= 0 && value == last) return true;
  if (prefs_.putUChar(key, value) != 1) return false;
  last = value;
  return true;
}

uint8_t DeviceSettingsStore::contrast() const {
  return constrain(prefs_.getUChar("contrast", hardware::kLcdContrast), 0, kMaxContrast);
}

bool DeviceSettingsStore::saveContrast(uint8_t contrast) {
  return saveCached("contrast", constrain(contrast, 0, kMaxContrast), lastContrast_);
}

uint8_t DeviceSettingsStore::backlightMode() const {
  return static_cast<uint8_t>(backlightModeFromStored(prefs_.getUChar("backlight", kBacklightModeDefault)));
}

bool DeviceSettingsStore::saveBacklightMode(uint8_t mode) {
  return saveCached("backlight", static_cast<uint8_t>(backlightModeFromStored(mode)), lastBacklightMode_);
}

bool DeviceSettingsStore::energySavingEnabled() const { return prefs_.getUChar("energy", 0) != 0; }

bool DeviceSettingsStore::saveEnergySavingEnabled(bool enabled) {
  return saveCached("energy", enabled ? 1 : 0, lastEnergySaving_);
}

uint8_t DeviceSettingsStore::sleepDelay() const {
  return static_cast<uint8_t>(sleepDelayFromStored(prefs_.getUChar("sleep_after", kSleepDelayDefault)));
}

bool DeviceSettingsStore::saveSleepDelay(uint8_t delay) {
  return saveCached("sleep_after", static_cast<uint8_t>(sleepDelayFromStored(delay)), lastSleepDelay_);
}

uint8_t DeviceSettingsStore::powerOffDelay() const {
  return static_cast<uint8_t>(powerOffDelayFromStored(prefs_.getUChar("off_after", kPowerOffDelayDefault)));
}

bool DeviceSettingsStore::savePowerOffDelay(uint8_t delay) {
  return saveCached("off_after", static_cast<uint8_t>(powerOffDelayFromStored(delay)), lastPowerOffDelay_);
}

bool DeviceSettingsStore::dockModeEnabled() const { return prefs_.getUChar("dock", 0) != 0; }

bool DeviceSettingsStore::saveDockModeEnabled(bool enabled) {
  return saveCached("dock", enabled ? 1 : 0, lastDockMode_);
}

}  // namespace inventatory_scan
