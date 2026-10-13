// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Power off with the '*' key. Holding '*' alone for a second shows a three-second countdown; holding
// on to its end sinks the mark behind the glass, and deep sleep stops the chip and the radios.
// Releasing '*' before the end cancels. The same key wakes the chip, and the wake restarts the
// firmware through setup().
//
// The battery is read here too: a low cell gets a warning and a shutdown, a critical one shuts
// down at once, and a low cell at boot is refused.

#include "app/inventatory_scan_app.h"

#include <esp_sleep.h>

namespace inventatory_scan {

namespace {

// A '*' still held this long after the commit cancels power off instead of waiting indefinitely.
constexpr unsigned long kPowerOffReleaseTimeoutMs = 5000UL;
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
  powerOffWarning_ = false;
  display_.clearOverlay();
  // Standby has already paused the network worker and suspended the scanner. A worker still in a
  // blocking HTTP or mDNS call does not cancel power off: deep sleep stops it, and a queued event is
  // already durable in NVS, so a sync cut short is the same as a power cut.
  if (!fromStandby && !pauseNetworkForSetupTransition()) {
    Serial.println("Network worker is busy; powering off without waiting for it.");
  }

  // The sink runs at once, even while '*' is still held: the chip sleeps only after the release.
  display_.playPowerOffSink();

  // Puts the screen back as it was when the power-off was cancelled.
  const auto restoreScreen = [&]() {
    if (fromStandby) {
      display_.setStandby(true);
      return;
    }
    resumeNetworkAfterSetupTransition();
    // Setup has no Ready screen; the pairing screen comes back instead.
    if (setupMode_) {
      display_.showSetup(provisioning_.pairingCode());
    } else {
      display_.showReady();
    }
  };

  const unsigned long deadline = millis() + kPowerOffReleaseTimeoutMs;
  while (keypadPowerKeyDown()) {
    if (static_cast<long>(deadline - millis()) <= 0) {
      Serial.println("Power off cancelled: release * first.");
      restoreScreen();
      return;
    }
    InventatoryKeyEvent ignored;
    keypadPoll(ignored);
    delay(5);
  }
  // Checked only now, so a press during the sink cancels instead of waking the chip at once.
  if (!keypadArmPowerKeyWake()) {
    Serial.println("Power off cancelled: * is still pressed.");
    restoreScreen();
    return;
  }
  display_.setStandby(true);
  backlight_.setLit(false);
  Serial.println("Powered off. Press * to wake.");
  esp_deep_sleep_start();
}

// The countdown starts once '*' has been held for a second. Like the low-battery warning, it takes
// the panel out of standby, so the countdown is always visible while it runs. The display drains the
// bar itself from here, one pixel at a time, until the commit or a release.
void InventatoryScanApp::startPowerOffWarning() {
  if (powerMode_ == PowerMode::Standby) exitStandby();
  Serial.println("Power off warning: release * to stay on.");
  powerOffWarning_ = true;
  noteUserActivity();
  display_.showPowerWarning();
}

// A cancel does nothing unless the countdown is on screen, so it never clears the low-battery overlay.
void InventatoryScanApp::cancelPowerOffWarning() {
  if (!powerOffWarning_) return;
  Serial.println("Power off cancelled: * released.");
  powerOffWarning_ = false;
  display_.clearOverlay();
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

// The card is for the Ready screen only: it must not cover a quantity being typed, a menu or a warning.
bool InventatoryScanApp::canShowBatteryCard() const {
  return !setupMode_ && powerMode_ == PowerMode::Normal && state_ == State::Idle && !otaRequested_ &&
         !otaActive_ && !lowBatteryWarning_ && !powerOffWarning_;
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
  // The low-battery overlay replaces any power-off countdown on screen, so that countdown ends here.
  powerOffWarning_ = false;
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
