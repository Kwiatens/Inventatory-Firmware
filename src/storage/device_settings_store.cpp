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
  return true;
}

uint8_t DeviceSettingsStore::contrast() const {
  return constrain(prefs_.getUChar("contrast", hardware::kLcdContrast), 0, kMaxContrast);
}

bool DeviceSettingsStore::saveContrast(uint8_t contrast) {
  const uint8_t value = constrain(contrast, 0, kMaxContrast);
  if (lastContrast_ >= 0 && value == lastContrast_) return true;
  if (prefs_.putUChar("contrast", value) != 1) return false;
  lastContrast_ = value;
  return true;
}

uint8_t DeviceSettingsStore::backlightMode() const {
  return static_cast<uint8_t>(backlightModeFromStored(prefs_.getUChar("backlight", kBacklightModeDefault)));
}

bool DeviceSettingsStore::saveBacklightMode(uint8_t mode) {
  const uint8_t value = static_cast<uint8_t>(backlightModeFromStored(mode));
  if (lastBacklightMode_ >= 0 && value == lastBacklightMode_) return true;
  if (prefs_.putUChar("backlight", value) != 1) return false;
  lastBacklightMode_ = value;
  return true;
}

}  // namespace inventatory_scan
