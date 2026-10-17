// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Power off with the '*' key. Holding '*' alone for a second shows a three-second countdown; holding
// on to its end sinks the mark behind the glass, and deep sleep stops the chip and the radios.
// Releasing '*' before the end cancels. The same key wakes the chip, and the wake restarts the
// firmware through setup().
//
// The battery is read here too: a low cell gets a warning and a shutdown, a critical one shuts
// down at once, and a low cell at boot is refused.
//
// Energy saving sleeps the device after an idle time and turns it off after a further time asleep.
// Each step is preceded by a countdown on screen, so the scanner never switches off without warning.

#include "app/inventatory_scan_app.h"
#include "InventatoryScanUiPolicy.h"

#include <esp_sleep.h>

namespace inventatory_scan {

namespace {

// A '*' still held this long after the commit cancels power off instead of waiting indefinitely.
constexpr unsigned long kPowerOffReleaseTimeoutMs = 5000UL;
// One battery reading per second, so the tracker's trip counters count seconds.
constexpr unsigned long kBatterySampleIntervalMs = 1000UL;
// The gauge state is written to NVS when the shown percent has moved this far, when a charge
// completes, and at least this often on battery.
constexpr int kBatterySaveStepPercent = 5;
constexpr unsigned long kBatterySaveIntervalMs = 900000UL;
// Time the low-battery warning gives the user to dock the scanner before it powers off.
constexpr unsigned long kLowBatteryShutdownMs = 60000UL;
// How long a boot refusal or a critical shutdown notice stays up before the chip sleeps.
constexpr unsigned long kBatteryNoticeMs = 4000UL;
// How long the card stays up after the dock is connected or removed.
constexpr unsigned long kBatteryCardMs = 2500UL;

}  // namespace

void InventatoryScanApp::enterPowerOff() {
  const bool fromStandby = powerMode_ == PowerMode::Standby;
  Serial.println("Power off requested.");
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
  saveBatteryState();
  esp_deep_sleep_start();
}

// The countdown starts once '*' has been held for a second. Like the low-battery warning, it takes
// the panel out of standby, so the countdown is always visible while it runs. The display drains the
// bar itself from here, one pixel at a time, until the commit or a release.
void InventatoryScanApp::startPowerOffWarning() {
  dismissEnergyWarning();
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
                             batteryTracker_.charging(), batteryTracker_.chargeDone() ? 100 : batteryTracker_.percent(),
                             batteryTracker_.estimateApproximate())) {
    return String();
  }
  return String(text);
}

// The card is for the Ready screen only: it must not cover a quantity being typed, a menu or a warning.
bool InventatoryScanApp::canShowBatteryCard() const {
  return !setupMode_ && powerMode_ == PowerMode::Normal && state_ == State::Idle && !otaRequested_ &&
         !otaActive_ && !lowBatteryWarning_ && !powerOffWarning_;
}

// The current the firmware draws now, in milliamps. A placeholder model of its states until the
// bench measurements are in (battery plan, phase 0).
uint32_t InventatoryScanApp::batteryLoadMilliamps() const {
  if (powerMode_ == PowerMode::Standby) return kBatteryLoadStandbyMa;
  uint32_t milliamps = kBatteryLoadBaseMa + kBatteryLoadScannerMa;
  if (backlight_.lit()) milliamps += kBatteryLoadBacklightMa;
  if (wifiStarted_) milliamps += kBatteryLoadWifiMa;
  return milliamps;
}

// Writes the gauge state to NVS so the next boot resumes from it. Nothing is written before the
// first reading.
void InventatoryScanApp::saveBatteryState() {
  if (!batteryTracker_.started()) return;
  batteryStore_.save(batteryTracker_.state());
  batterySavedPercent_ = batteryTracker_.percent();
  lastBatterySaveAt_ = millis();
}

// Saves when the shown percent has moved, when a charge completes, and every kBatterySaveIntervalMs
// on battery. Few writes keep the NVS partition healthy.
void InventatoryScanApp::saveBatteryStateIfDue(unsigned long now) {
  const int percent = batteryTracker_.percent();
  if (percent < 0) return;
  const bool moved = batterySavedPercent_ < 0 || percent - batterySavedPercent_ >= kBatterySaveStepPercent ||
                     batterySavedPercent_ - percent >= kBatterySaveStepPercent;
  const bool full = percent == 100 && batterySavedPercent_ != 100;
  const bool due = !batteryTracker_.charging() && now - lastBatterySaveAt_ >= kBatterySaveIntervalMs;
  if (moved || full || due) saveBatteryState();
}

void InventatoryScanApp::serviceBattery() {
  const unsigned long now = millis();
  if (batterySampled_ && now - lastBatterySampleAt_ < kBatterySampleIntervalMs) return;
  batterySampled_ = true;
  lastBatterySampleAt_ = now;

  const ChargeState charge = battery_.chargeState();
  batteryTracker_.update(battery_.readMillivolts(), charge != ChargeState::Unplugged, now,
                         charge == ChargeState::Done, batteryLoadMilliamps());
  const bool chargeDone = batteryTracker_.chargeDone();
  display_.setBatteryUnreadable(batteryTracker_.unreadable());
  const bool charging = batteryTracker_.charging();
  const int percent = batteryTracker_.percent();
  const String estimate = batteryEstimateText();
  display_.setBatteryStatus(percent, charging, chargeDone, estimate);
  saveBatteryStateIfDue(now);

  if (charging != lastCharging_ || chargeDone != lastChargeDone_) {
    lastCharging_ = charging;
    lastChargeDone_ = chargeDone;
    noteUserActivity();
    if (charging && lowBatteryWarning_) cancelLowBatteryWarning();
    if (canShowBatteryCard()) display_.showBatteryCard(charging, chargeDone, percent, estimate, kBatteryCardMs);
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
  Serial.printf("Battery low: dock the scanner, or it powers off in %lu s.\r\n", kLowBatteryShutdownMs / 1000UL);
  // The low-battery overlay replaces any power-off countdown on screen, so that countdown ends here.
  powerOffWarning_ = false;
  dismissEnergyWarning();
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
  display_.showCountdownWarning(String("Battery low"), String("Dock to charge"),
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
  display_.showCountdownWarning(title, detail, String("Powering off"), -1, 0);
  const unsigned long until = millis() + kBatteryNoticeMs;
  while (static_cast<long>(until - millis()) > 0) {
    display_.tick();
    delay(20);
  }
  powerOffForBattery();
}

// Energy saving. Idle time on the Ready screen sleeps the device (standby): the screen goes dark, sync
// pauses and the scanner switches off. Asleep for the set time, it turns off (deep sleep). A countdown
// on screen comes before each step, and a key press or a scan cancels it. energyNextAction() decides
// the step from the phase and the time left; this code carries the step out.
void InventatoryScanApp::serviceEnergySaving() {
  const unsigned long now = millis();
  EnergyInputs in;
  in.enabled = energySavingOn_;
  in.powerOffSet = powerOffEnabled(powerOffDelay_);
  in.idleReady = powerMode_ == PowerMode::Normal && state_ == State::Idle && !setupMode_ &&
                 !lowBatteryWarning_ && !powerOffWarning_ && !display_.errorVisible();
  in.updating = otaRequested_ || otaActive_;
  in.sleepRemainingMs = energyRemainingMs(now, lastUserActivityAt_, sleepDelayMs(sleepDelay_));
  in.offRemainingMs = energyRemainingMs(now, sleepStartedAt_, powerOffDelayMs(powerOffDelay_));

  switch (energyNextAction(energyPhase_, in)) {
    case EnergyAction::None:
      break;
    case EnergyAction::StartSleepWarning:
      startEnergySleepWarning();
      break;
    case EnergyAction::Sleep:
      enterEnergySleep();
      return;
    case EnergyAction::StartOffWarning:
      startEnergyOffWarning();
      break;
    case EnergyAction::PowerOff:
      powerOffForEnergySaving();
      return;
    case EnergyAction::CancelWarning:
      dismissEnergyWarning();
      return;
  }
  if (energyWarningShown()) updateEnergyWarning(now);
}

void InventatoryScanApp::startEnergySleepWarning() {
  Serial.println("Energy saving: sleep countdown started; a key or scan keeps the device on.");
  energyPhase_ = EnergyPhase::SleepWarning;
  energyShownSeconds_ = -1;
}

void InventatoryScanApp::enterEnergySleep() {
  Serial.println("Energy saving: idle for the set time, entering standby.");
  dismissEnergyWarning();
  enterStandby();
  energyPhase_ = EnergyPhase::Asleep;
  sleepStartedAt_ = millis();
}

// The power-off countdown wakes the panel, scanner and network first, so the device is awake when it
// turns off and the warning can be seen.
void InventatoryScanApp::startEnergyOffWarning() {
  Serial.println("Energy saving: power off countdown started; a key keeps the device on.");
  exitStandby();
  energyPhase_ = EnergyPhase::OffWarning;
  energyShownSeconds_ = -1;
}

// The countdown is drawn in whole seconds. It is redrawn only when the count changes, not on every loop.
void InventatoryScanApp::updateEnergyWarning(unsigned long now) {
  const bool sleeping = energyPhase_ == EnergyPhase::SleepWarning;
  const uint32_t remainingMs =
      sleeping ? energyRemainingMs(now, lastUserActivityAt_, sleepDelayMs(sleepDelay_))
               : energyRemainingMs(now, sleepStartedAt_, powerOffDelayMs(powerOffDelay_));
  const int seconds = static_cast<int>(energyCountdownSeconds(remainingMs));
  if (seconds == energyShownSeconds_) return;
  energyShownSeconds_ = seconds;
  display_.showCountdownWarning(String(sleeping ? "Going to sleep" : "Turning off"), String("Any key stays on"),
                                String(sleeping ? "Sleeping in " : "Powering off in ") + String(seconds) + " s",
                                seconds, energyCountdownPercent(remainingMs));
}

void InventatoryScanApp::dismissEnergyWarning() {
  if (!energyWarningShown()) return;
  energyPhase_ = EnergyPhase::Awake;
  energyShownSeconds_ = -1;
  display_.clearOverlay();
}

void InventatoryScanApp::powerOffForEnergySaving() {
  Serial.println("Energy saving: powering off.");
  dismissEnergyWarning();
  enterPowerOff();
}

}  // namespace inventatory_scan
