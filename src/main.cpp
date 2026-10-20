// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#include <Arduino.h>
#include <esp_system.h>
#ifdef CONFIG_APP_ROLLBACK_ENABLE
#include <esp_ota_ops.h>
#endif

#include "app/inventatory_scan_app.h"
#include "log/device_log.h"

using namespace inventatory_scan;

static InventatoryScanApp app;
static bool appReady = false;

#ifdef CONFIG_APP_ROLLBACK_ENABLE
// The Arduino core marks a new image valid before setup(), defeating rollback. Overriding this
// weak hook keeps an OTA image pending until confirmFirmwareWhenHealthy() runs; a crash before
// then rolls back to the previous image.
extern "C" bool verifyRollbackLater() { return true; }

static constexpr unsigned long kHealthyUptimeMs = 30000UL;

static void confirmFirmwareWhenHealthy() {
  static bool done = false;
  if (done || millis() < kHealthyUptimeMs) return;
  done = true;
  if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
    deviceLog.println("Firmware confirmed after a healthy start.");
  }
}
#else
static void confirmFirmwareWhenHealthy() {}
#endif

void setup() {
  Serial.begin(115200);
  const unsigned long start = millis();
#if ARDUINO_USB_CDC_ON_BOOT
  while (!Serial && millis() - start < 3000UL) {
    delay(10);
  }
#else
  delay(1000);
#endif
  deviceLog.printf("Reset reason: %d\r\n", static_cast<int>(esp_reset_reason()));
  appReady = app.begin();
}

void loop() {
  if (appReady) {
    app.loop();
    confirmFirmwareWhenHealthy();
  } else {
    delay(1000);
  }
}
