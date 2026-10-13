// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Power off with the '*' key. Deep sleep stops the chip and the radios; the same key wakes it,
// and the wake restarts the firmware through setup().
//
// The battery is read here too: a low cell gets a warning and a shutdown, a critical one shuts
// down at once, and a low cell at boot is refused.

#include "app/inventatory_scan_app.h"

#include <esp_sleep.h>

namespace inventatory_scan {

namespace {

// A '*' still held after this long cancels power off instead of waiting indefinitely.
constexpr unsigned long kPowerOffReleaseTimeoutMs = 5000UL;
// Time the "Powering off" screen stays up before the panel is blanked.
constexpr unsigned long kPowerOffMessageMs = 600UL;
// One battery reading per second, so the tracker's trip counters count seconds.
constexpr unsigned long kBatterySampleIntervalMs = 1000UL;
// Time the low-battery warning gives the user to dock the scanner before it powers off.
constexpr unsigned long kLowBatteryShutdownMs = 60000UL;
// How long a boot refusal or a critical shutdown notice stays up before the chip sleeps.
constexpr unsigned long kBatteryNoticeMs = 4000UL;
// How long the card stays up after the dock is connected or removed.
constexpr unsigned long kBatteryCardMs = 2500UL;

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
  backlight_.setLit(false);
  Serial.println("Powered off. Press * to wake.");
  esp_deep_sleep_start();
}

void InventatoryScanApp::noteUserActivity() { lastUserActivityAt_ = millis(); }

String InventatoryScanApp::batteryEstimateText() const {
  char text[32];
  if (!formatBatteryEstimate(text, sizeof(text), batteryTracker_.minutesRemaining(),
                             batteryTracker_.charging(), batteryTracker_.percent())) {
    return String();
  }
  return String(text);
}

// The card is for the Ready screen only: it must not cover a quantity being typed or a menu.
bool InventatoryScanApp::canShowBatteryCard() const {
  return !setupMode_ && powerMode_ == PowerMode::Normal && state_ == State::Idle && !otaRequested_ &&
         !otaActive_ && !lowBatteryWarning_;
}

void InventatoryScanApp::serviceBattery() {
  const unsigned long now = millis();
  if (batterySampled_ && now - lastBatterySampleAt_ < kBatterySampleIntervalMs) return;
  batterySampled_ = true;
  lastBatterySampleAt_ = now;

  batteryTracker_.update(battery_.readMillivolts(), battery_.chargerPresent(), now);
  const bool charging = batteryTracker_.charging();
  const int percent = batteryTracker_.percent();
  const String estimate = batteryEstimateText();
  display_.setBatteryStatus(percent, charging, estimate);

  if (charging != lastCharging_) {
    lastCharging_ = charging;
    noteUserActivity();
    if (charging && lowBatteryWarning_) cancelLowBatteryWarning();
    if (canShowBatteryCard()) display_.showBatteryCard(charging, percent, estimate, kBatteryCardMs);
  }

  if (lowBatteryWarning_) {
    updateLowBatteryWarning(now);
    return;
  }
  switch (batteryTracker_.level()) {
    case BatteryLevel::Critical:
      powerOffWithNotice(String("Battery empty"), String("Charge to power on"));
      return;
    case BatteryLevel::Low:
      // An update in progress is not interrupted by a warning; a critical cell still powers off.
      if (!otaRequested_ && !otaActive_) startLowBatteryWarning();
      return;
    case BatteryLevel::Ok:
      return;
  }
}

void InventatoryScanApp::startLowBatteryWarning() {
  Serial.println("Battery low: dock the scanner, or it powers off in 60 s.");
  if (powerMode_ == PowerMode::Standby) exitStandby();
  lowBatteryWarning_ = true;
  lowBatteryWarningAt_ = millis();
  lowBatteryShownSeconds_ = -1;
  noteUserActivity();
  updateLowBatteryWarning(lowBatteryWarningAt_);
}

// The countdown is drawn in whole seconds; the bar empties with the same clock.
void InventatoryScanApp::updateLowBatteryWarning(unsigned long now) {
  const unsigned long elapsed = now - lowBatteryWarningAt_;
  if (elapsed >= kLowBatteryShutdownMs) {
    powerOffForBattery();
    return;
  }
  const int seconds = static_cast<int>((kLowBatteryShutdownMs - elapsed + 999UL) / 1000UL);
  if (seconds == lowBatteryShownSeconds_) return;
  lowBatteryShownSeconds_ = seconds;
  const int barPercent = static_cast<int>((kLowBatteryShutdownMs - elapsed) * 100UL / kLowBatteryShutdownMs);
  display_.showLowBattery(String("Battery low"), String("Dock to charge"),
                          String("Powering off in ") + String(seconds) + " s", seconds, barPercent);
}

void InventatoryScanApp::cancelLowBatteryWarning() {
  Serial.println("Dock connected: low-battery warning cleared.");
  lowBatteryWarning_ = false;
  lowBatteryShownSeconds_ = -1;
  display_.clearOverlay();
}

void InventatoryScanApp::powerOffForBattery() {
  if (powerMode_ == PowerMode::Standby) exitStandby();
  display_.clearOverlay();
  enterPowerOff();
}

// Shows a message on its own screen for a moment, then powers off. Used when the
// battery cannot run the device, so the user learns why it went dark.
void InventatoryScanApp::powerOffWithNotice(const String& title, const String& detail) {
  if (powerMode_ == PowerMode::Standby) exitStandby();
  noteUserActivity();
  display_.showLowBattery(title, detail, String("Powering off"), -1, 0);
  const unsigned long until = millis() + kBatteryNoticeMs;
  while (static_cast<long>(until - millis()) > 0) {
    display_.tick();
    delay(20);
  }
  powerOffForBattery();
}

}  // namespace inventatory_scan
