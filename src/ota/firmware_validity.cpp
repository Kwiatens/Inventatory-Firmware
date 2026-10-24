// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#include "ota/firmware_validity.h"

#include "config/version.h"
#include "log/device_log.h"

#include <Arduino.h>
#include <Preferences.h>
#ifdef CONFIG_APP_ROLLBACK_ENABLE
#include <esp_ota_ops.h>
#endif

#ifdef CONFIG_APP_ROLLBACK_ENABLE
// The Arduino core marks a new image valid before setup(), defeating rollback. Overriding this
// weak hook keeps an OTA image pending until confirmRunningFirmware() runs.
extern "C" bool verifyRollbackLater() { return true; }
#endif

// The firmware identity that tools/sign_firmware.py reads from firmware.bin. Nothing reads it at run time;
// the linker keeps it through -u inventatory_firmware_identity in platformio.ini.
extern "C" __attribute__((used)) const char inventatory_firmware_identity[] = INVENTATORY_FIRMWARE_IDENTITY;

namespace inventatory_scan {

namespace {

constexpr char kOtaNamespace[] = "inv_ota";
static_assert(sizeof(kOtaNamespace) <= 16, "NVS namespace exceeds ESP32's 15-character limit");
constexpr char kMinimumCounterKey[] = "min_counter";

uint32_t storedMinimumCounter() {
  Preferences prefs;
  if (!prefs.begin(kOtaNamespace, true)) return 0;
  const uint32_t stored = prefs.getUInt(kMinimumCounterKey, 0);
  prefs.end();
  return stored;
}

// Best effort: a failed write leaves the minimum at the running image's counter, which still applies.
void raiseMinimumCounter() {
  if (storedMinimumCounter() >= kFirmwareSecurityCounter) return;
  Preferences prefs;
  if (!prefs.begin(kOtaNamespace, false)) return;
  if (prefs.putUInt(kMinimumCounterKey, kFirmwareSecurityCounter) != sizeof(uint32_t)) {
    deviceLog.println("Could not store the update security counter.");
  }
  prefs.end();
}

}  // namespace

uint32_t minimumFirmwareSecurityCounter() {
  const uint32_t stored = storedMinimumCounter();
  return stored > kFirmwareSecurityCounter ? stored : kFirmwareSecurityCounter;
}

void confirmRunningFirmware(const char* reason) {
#ifdef CONFIG_APP_ROLLBACK_ENABLE
  static bool confirmed = false;
  if (confirmed) return;
  esp_ota_img_states_t state;
  const esp_partition_t* running = esp_ota_get_running_partition();
  // Only a pending image needs confirming; an image flashed over USB has no OTA state to change. The state
  // is read once, since the loop calls this on every pass after 30 s.
  if (running == nullptr || esp_ota_get_state_partition(running, &state) != ESP_OK ||
      state != ESP_OTA_IMG_PENDING_VERIFY) {
    confirmed = true;
    return;
  }
  if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
    confirmed = true;
    deviceLog.print("Firmware confirmed: ");
    deviceLog.println(reason);
    raiseMinimumCounter();
  }
#else
  (void)reason;
#endif
}

void reportFirmwareRollback() {
#ifdef CONFIG_APP_ROLLBACK_ENABLE
  if (esp_ota_get_last_invalid_partition() != nullptr) {
    deviceLog.println("An earlier firmware update did not confirm and was rolled back; this is the previous image.");
  }
#endif
}

}  // namespace inventatory_scan
