// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#include <Arduino.h>
#include <esp_system.h>

#include "app/inventatory_scan_app.h"
#include "log/device_log.h"
#include "ota/firmware_validity.h"

using namespace inventatory_scan;

static InventatoryScanApp app;
static bool appReady = false;

static constexpr unsigned long kHealthyUptimeMs = 30000UL;

static void confirmFirmwareWhenHealthy() {
  if (millis() >= kHealthyUptimeMs) confirmRunningFirmware("healthy start");
}

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
  reportFirmwareRollback();
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
