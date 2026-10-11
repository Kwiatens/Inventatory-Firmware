// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Power off with the '*' key. Deep sleep stops the chip and the radios; the same key wakes it,
// and the wake restarts the firmware through setup().

#include "app/inventatory_scan_app.h"

#include <esp_sleep.h>

namespace inventatory_scan {

namespace {

// A '*' still held after this long cancels power off instead of waiting indefinitely.
constexpr unsigned long kPowerOffReleaseTimeoutMs = 5000UL;
// Time the "Powering off" screen stays up before the panel is blanked.
constexpr unsigned long kPowerOffMessageMs = 600UL;

}  // namespace

void InventatoryScanApp::enterPowerOff() {
  const bool fromStandby = powerMode_ == PowerMode::Standby;
  Serial.println("Power off requested with the * key.");
  // Standby has already paused the network worker and suspended the scanner.
  if (!fromStandby && !pauseNetworkForSetupTransition()) {
    Serial.println("Network worker is busy; power off postponed.");
    return;
  }

  const unsigned long deadline = millis() + kPowerOffReleaseTimeoutMs;
  while (keypadPowerKeyDown()) {
    if (static_cast<long>(deadline - millis()) <= 0) {
      Serial.println("Power off cancelled: release * first.");
      if (!fromStandby) resumeNetworkAfterSetupTransition();
      return;
    }
    InventatoryKeyEvent ignored;
    keypadPoll(ignored);
    delay(5);
  }

  display_.showPowerOff();
  display_.tick();
  delay(kPowerOffMessageMs);
  // Checked only now, so a press during the message cancels instead of waking the chip at once.
  if (!keypadArmPowerKeyWake()) {
    Serial.println("Power off cancelled: * is still pressed.");
    if (fromStandby) {
      display_.setStandby(true);
    } else {
      resumeNetworkAfterSetupTransition();
      display_.showReady();
    }
    return;
  }
  display_.setStandby(true);
  Serial.println("Powered off. Press * to wake.");
  esp_deep_sleep_start();
}

}  // namespace inventatory_scan
