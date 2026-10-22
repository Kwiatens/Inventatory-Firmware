// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Power off on '*' hold (countdown at 1 s, deep sleep at 4 s), energy saving, dock mode and
// battery shutdowns. Only '*' wakes from deep sleep. Dock standby keeps the chip awake and polls the charger,
// because the charger pins cannot wake a deep sleep with the current divider. The scanner's module sleeps in it.

#include "app/inventatory_scan_app.h"
#include "InventatoryScanUiPolicy.h"
#include "config/hardware.h"
#include "log/device_log.h"
#include "ota/firmware_validity.h"

#include <esp_sleep.h>

namespace inventatory_scan {

namespace {

// A '*' still held this long after the commit cancels power off.
constexpr unsigned long kPowerOffReleaseTimeoutMs = 5000UL;
// One reading per second, so the tracker's trip counters count seconds.
constexpr unsigned long kBatterySampleIntervalMs = 1000UL;
// Gauge saved to NVS on this percent move, on charge completion, and this often on battery.
constexpr int kBatterySaveStepPercent = 5;
constexpr unsigned long kBatterySaveIntervalMs = 900000UL;
// Time to dock before a low-battery warning powers off.
constexpr unsigned long kLowBatteryShutdownMs = 60000UL;
// How long a boot refusal or critical notice stays up before sleep.
constexpr unsigned long kBatteryNoticeMs = 4000UL;
// How long the dock card stays up.
constexpr unsigned long kBatteryCardMs = 2500UL;
// Dock mode: standby this long after the charger is plugged in. The charge card shows first.
constexpr unsigned long kDockPowerOffDelayMs = 5000UL;
// Serial status-line interval.
constexpr unsigned long kBatteryLogIntervalMs = 10000UL;
// Charger sample interval. A removal is seen within this time, even in dock standby.
constexpr unsigned long kChargerSampleIntervalMs = 100UL;

}  // namespace

// Deep sleep. Only '*' wakes the device: the charger pins cannot wake it with the current divider.
void InventatoryScanApp::enterPowerOff() {
  const bool fromStandby = powerMode_ == PowerMode::Standby;
  deviceLog.println("Power off requested.");
  powerOffWarning_ = false;
  display_.clearOverlay();
  // A worker still in a blocking HTTP or mDNS call does not stop power off: deep sleep ends it, and
  // queued events are already durable, so a cut-short sync is like a power cut.
  if (!fromStandby && !pauseNetworkForSetupTransition()) {
    deviceLog.println("Network worker is busy; powering off without waiting for it.");
  }

  // The sink runs at once, even while '*' is still held: the chip sleeps only after the release.
  display_.playPowerOffSink();

  // Restores the screen after a cancelled power-off.
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
      deviceLog.println("Power off cancelled: release * first.");
      restoreScreen();
      return;
    }
    InventatoryKeyEvent ignored;
    keypadPoll(ignored);
    delay(5);
  }
  // Checked only now, so a press during the sink cancels instead of waking the chip at once.
  if (!keypadArmPowerKeyWake()) {
    deviceLog.println("Power off cancelled: * is still pressed.");
    restoreScreen();
    return;
  }
  display_.setStandby(true);
  backlight_.setLit(false);
  // Chip sleep does not cut scanner power, so the module sleeps first.
  scanner_.deepSleep();
  deviceLog.println("Powered off. Press * to wake.");
  saveBatteryState();
  // Waking from deep sleep is a reset: an update still pending would be rolled back.
  if (loopStarted_) confirmRunningFirmware("powered off after a healthy start");
  esp_deep_sleep_start();
}

// Starts after '*' is held for a second. Like the low-battery warning, it leaves standby
// so the countdown stays visible.
void InventatoryScanApp::startPowerOffWarning() {
  dismissEnergyWarning();
  if (powerMode_ == PowerMode::Standby) exitStandby();
  deviceLog.println("Power off warning: release * to stay on.");
  powerOffWarning_ = true;
  noteUserActivity();
  display_.showPowerWarning();
}

// Does nothing unless the countdown is on screen, so it never clears the low-battery overlay.
void InventatoryScanApp::cancelPowerOffWarning() {
  if (!powerOffWarning_) return;
  deviceLog.println("Power off cancelled: * released.");
  powerOffWarning_ = false;
  display_.clearOverlay();
}

void InventatoryScanApp::noteUserActivity() { lastUserActivityAt_ = millis(); }

String InventatoryScanApp::batteryEstimateText() const {
  char text[32];
  if (!formatBatteryEstimate(text, sizeof(text), batteryTracker_.minutesRemaining(), batteryTracker_.docked(),
                             batteryTracker_.chargeDone() ? 100 : batteryTracker_.percent(),
                             batteryTracker_.estimateApproximate())) {
    return String();
  }
  return String(text);
}

// Ready screen only: the card must not cover a quantity, menu or warning.
bool InventatoryScanApp::canShowBatteryCard() const {
  return !setupMode_ && powerMode_ == PowerMode::Normal && state_ == State::Idle && !otaRequested_ &&
         !otaActive_ && !lowBatteryWarning_ && !powerOffWarning_;
}

// Estimated mA by firmware state. Placeholder until bench measurements (battery plan, phase 0).
uint32_t InventatoryScanApp::batteryLoadMilliamps() const {
  if (powerMode_ == PowerMode::Standby) return kBatteryLoadStandbyMa;
  uint32_t milliamps = kBatteryLoadBaseMa + kBatteryLoadScannerMa;
  if (backlight_.lit()) milliamps += kBatteryLoadBacklightMa;
  if (wifiStarted_) milliamps += kBatteryLoadWifiMa;
  return milliamps;
}

// Writes the gauge state to NVS for the next boot. Nothing is written before the first reading.
void InventatoryScanApp::saveBatteryState() {
  if (!batteryTracker_.started()) return;
  batteryStore_.save(batteryTracker_.state());
  batterySavedPercent_ = batteryTracker_.percent();
  lastBatterySaveAt_ = millis();
}

// Saves on a percent move, on charge completion, and every kBatterySaveIntervalMs on battery.
// Few writes keep NVS healthy.
void InventatoryScanApp::saveBatteryStateIfDue(unsigned long now) {
  const int percent = batteryTracker_.percent();
  if (percent < 0) return;
  const bool moved = batterySavedPercent_ < 0 || percent - batterySavedPercent_ >= kBatterySaveStepPercent ||
                     batterySavedPercent_ - percent >= kBatterySaveStepPercent;
  const bool full = percent == 100 && batterySavedPercent_ != 100;
  const bool due = !batteryTracker_.docked() && now - lastBatterySaveAt_ >= kBatterySaveIntervalMs;
  if (moved || full || due) saveBatteryState();
}

void InventatoryScanApp::serviceBattery() {
  const unsigned long now = millis();
  if (batterySampled_ && now - lastBatterySampleAt_ < kBatterySampleIntervalMs) return;
  batterySampled_ = true;
  lastBatterySampleAt_ = now;

  const uint32_t batteryMv = battery_.readMillivolts();
  batteryTracker_.update(batteryMv, chargerLedLit_, now, batteryLoadMilliamps(), chargerPlugged_);
  const bool chargeDone = batteryTracker_.chargeDone();
  display_.setBatteryUnreadable(batteryTracker_.unreadable());
  const bool docked = batteryTracker_.docked();
  const int percent = batteryTracker_.percent();
  const String estimate = batteryEstimateText();
  display_.setBatteryStatus(percent, docked, chargeDone, estimate);
  saveBatteryStateIfDue(now);

  // Status line for bench checks: cell voltage and both charger midpoints.
  if (lastBatteryLogAt_ == 0 || now - lastBatteryLogAt_ >= kBatteryLogIntervalMs) {
    lastBatteryLogAt_ = now;
    deviceLog.printf("Battery %lu mV, charger input %lu mV (%s), LED %lu mV (%s).\r\n",
                  static_cast<unsigned long>(batteryMv), static_cast<unsigned long>(battery_.connectorMillivolts()),
                  chargerPlugged_ ? "plugged" : "unplugged", static_cast<unsigned long>(battery_.chargingLedMillivolts()),
                  chargerLedLit_ ? "lit" : "dark");
  }

  if (docked != lastCharging_ || chargeDone != lastChargeDone_) {
    lastCharging_ = docked;
    lastChargeDone_ = chargeDone;
    noteUserActivity();
    if (docked && lowBatteryWarning_) cancelLowBatteryWarning();
    if (canShowBatteryCard()) display_.showBatteryCard(docked, chargeDone, percent, estimate, kBatteryCardMs);
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

// Samples both charger signals every kChargerSampleIntervalMs, with hysteresis, and acts on
// plug-in and removal edges.
void InventatoryScanApp::serviceChargerInput(unsigned long now) {
  if (now - lastChargerSampleAt_ >= kChargerSampleIntervalMs) {
    lastChargerSampleAt_ = now;
    chargerLedLit_ = signalHighNext(chargerLedLit_, battery_.chargingLedMillivolts());
    chargerPlugged_ = signalHighNext(chargerPlugged_, battery_.connectorMillivolts());
    if (chargerPlugged_ && !lastChargerPlugged_) onPlugged(now);
    if (!chargerPlugged_ && lastChargerPlugged_) onUnplugged();
    lastChargerPlugged_ = chargerPlugged_;
  }
  // Dock standby ends if the device is woken another way, e.g. by holding D.
  if (dockStandby_ && powerMode_ != PowerMode::Standby) dockStandby_ = false;
  serviceDockPowerOff(now);
}

// Plug-in wakes a standby device. With dock mode on, standby follows after kDockPowerOffDelayMs,
// so the card shows first.
void InventatoryScanApp::onPlugged(unsigned long now) {
  deviceLog.println("Charger input plugged in.");
  if (powerMode_ == PowerMode::Standby) exitStandby();
  if (!dockMode_) return;
  deviceLog.println("Dock mode: standby shortly while on the charger.");
  dockPowerOffPending_ = true;
  dockPowerOffAt_ = now + kDockPowerOffDelayMs;
}

// Charger removed: dock standby ends at once. A scheduled but not yet started standby is dropped.
void InventatoryScanApp::onUnplugged() {
  deviceLog.println("Charger input unplugged.");
  dockPowerOffPending_ = false;
  if (!dockStandby_) return;
  dockStandby_ = false;
  exitStandby();
}

// Starts dock standby once the charge card has shown, if still idle and on the charger.
void InventatoryScanApp::serviceDockPowerOff(unsigned long now) {
  if (!dockPowerOffPending_ || static_cast<long>(now - dockPowerOffAt_) < 0) return;
  dockPowerOffPending_ = false;
  if (!dockMode_ || !chargerPlugged_ || !canShowBatteryCard()) {
    deviceLog.println("Dock mode: standby skipped, the device is in use or no longer on the charger.");
    return;
  }
  dockStandby_ = true;
  // The scanner goes to deep sleep with its lamps off, as a power-off does. Standby alone only closes its port.
  scanner_.deepSleep();
  enterStandby();
}

void InventatoryScanApp::startLowBatteryWarning() {
  deviceLog.printf("Battery low: dock the scanner, or it powers off in %lu s.\r\n", kLowBatteryShutdownMs / 1000UL);
  // The low-battery overlay replaces a power-off countdown, which ends here.
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
  deviceLog.println("Dock connected: low-battery warning cleared.");
  lowBatteryWarning_ = false;
  lowBatteryShownSeconds_ = -1;
  display_.clearOverlay();
}

void InventatoryScanApp::powerOffForBattery() {
  if (powerMode_ == PowerMode::Standby) exitStandby();
  display_.clearOverlay();
  enterPowerOff();
}

// Shows a notice for a moment, then powers off, so the user learns why the device went dark.
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

// Energy saving: idle time on Ready sleeps the device (standby). After the set time asleep it
// powers off (deep sleep). A countdown precedes each step; a key or scan cancels it.
void InventatoryScanApp::serviceEnergySaving() {
  const unsigned long now = millis();
  EnergyInputs in;
  in.enabled = energySavingOn_;
  in.powerOffSet = powerOffEnabled(powerOffDelay_);
  // On the charger energy saving never sleeps the device; dock mode puts it in standby instead.
  in.idleReady = powerMode_ == PowerMode::Normal && state_ == State::Idle && !setupMode_ &&
                 !lowBatteryWarning_ && !powerOffWarning_ && !display_.errorVisible() && !chargerPlugged_;
  in.updating = otaRequested_ || otaActive_;
  in.onCharger = chargerPlugged_;
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
  deviceLog.println("Energy saving: sleep countdown started; a key or scan keeps the device on.");
  energyPhase_ = EnergyPhase::SleepWarning;
  energyShownSeconds_ = -1;
}

void InventatoryScanApp::enterEnergySleep() {
  deviceLog.println("Energy saving: idle for the set time, entering standby.");
  dismissEnergyWarning();
  // The scanner goes to deep sleep with its lamps off, as a power-off does. Standby alone only closes its port.
  scanner_.deepSleep();
  enterStandby();
  energyPhase_ = EnergyPhase::Asleep;
  sleepStartedAt_ = millis();
}

// Wakes the panel, scanner and network first, so the warning can be seen before power off.
void InventatoryScanApp::startEnergyOffWarning() {
  deviceLog.println("Energy saving: power off countdown started; a key keeps the device on.");
  exitStandby();
  energyPhase_ = EnergyPhase::OffWarning;
  energyShownSeconds_ = -1;
}

// Redrawn only when the whole-second count changes.
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
  deviceLog.println("Energy saving: powering off.");
  dismissEnergyWarning();
  enterPowerOff();
}

}  // namespace inventatory_scan
