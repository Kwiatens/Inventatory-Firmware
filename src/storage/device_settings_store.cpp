// Inventatory Scan R1 device preferences kept independently from Wi-Fi provisioning.

#include "storage/device_settings_store.h"

#include "config/hardware.h"

namespace inventatory_scan {

namespace {
constexpr char kSettingsNamespace[] = "inv_device";
constexpr uint8_t kMaxContrast = 63;
}  // namespace

bool DeviceSettingsStore::begin() { return prefs_.begin(kSettingsNamespace, false); }

uint8_t DeviceSettingsStore::contrast() const {
  return constrain(prefs_.getUChar("contrast", hardware::kLcdContrast), 0, kMaxContrast);
}

bool DeviceSettingsStore::saveContrast(uint8_t contrast) {
  return prefs_.putUChar("contrast", constrain(contrast, 0, kMaxContrast)) == 1;
}

}  // namespace inventatory_scan
