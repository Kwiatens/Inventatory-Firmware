// Inventatory Scan R1 device preferences kept independently from Wi-Fi provisioning.
#pragma once

#include <Arduino.h>
#include <Preferences.h>

namespace hims_scan {

class DeviceSettingsStore {
 public:
  bool begin();
  uint8_t contrast() const;
  bool saveContrast(uint8_t contrast);

 private:
  mutable Preferences prefs_;
};

}  // namespace hims_scan
