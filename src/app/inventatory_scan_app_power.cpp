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
//
// Dock mode turns the device off shortly after the charger is plugged in. Off here means the standby: the
// screen is dark and the scanner and network are paused, but the chip stays awake and polls the charger input,
// so taking the charger off brings the device back at once. A deep sleep cannot do that with the current
// divider, because the charger pins read about 2.2 V, which a digital wake reads as low.

#include "app/inventatory_scan_app.h"
#include "InventatoryScanUiPolicy.h"
#include "config/hardware.h"

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
// With dock mode on, the device turns off this long after the charger starts charging it. The charging
// card shows first.
constexpr unsigned long kDockPowerOffDelayMs = 5000UL;
// How often the battery and charger input status line is written to the serial log.
constexpr unsigned long kBatteryLogIntervalMs = 10000UL;
// How often the charger LED and charger input are sampled. A removal is seen within this time, even in
// dock mode's standby, where the battery is not read.
constexpr unsigned long kChargerSampleIntervalMs = 100UL;

}  // namespace

// Power off into deep sleep. Only '*' wakes the device from it: the charger pins cannot wake a deep sleep with
// the current divider.
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
  // The chip's sleep does not cut the scanner's power, so the module is put to sleep first.
  scanner_.deepSleep();
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
  if (!formatBatteryEstimate(text, sizeof(text), batteryTracker_.minutesRemaining(), batteryTracker_.docked(),
                             batteryTracker_.chargeDone() ? 100 : batteryTracker_.percent(),
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

  // The status line for the bench check: the cell voltage, the two charger midpoints and their states.
  if (lastBatteryLogAt_ == 0 || now - lastBatteryLogAt_ >= kBatteryLogIntervalMs) {
    lastBatteryLogAt_ = now;
    Serial.printf("Battery %lu mV, charger input %lu mV (%s), LED %lu mV (%s).\r\n",
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

// Samples the charger's two signals every kChargerSampleIntervalMs, with hysteresis, and acts on the plug-in
// and removal edges. It runs in every loop, so a removal is seen within that interval even in dock standby.
void InventatoryScanApp::serviceChargerInput(unsigned long now) {
  if (now - lastChargerSampleAt_ >= kChargerSampleIntervalMs) {
    lastChargerSampleAt_ = now;
    chargerLedLit_ = signalHighNext(chargerLedLit_, battery_.chargingLedMillivolts());
    chargerPlugged_ = signalHighNext(chargerPlugged_, battery_.connectorMillivolts());
    if (chargerPlugged_ && !lastChargerPlugged_) onPlugged(now);
    if (!chargerPlugged_ && lastChargerPlugged_) onUnplugged();
    lastChargerPlugged_ = chargerPlugged_;
  }
  // Dock standby ends when the device is woken some other way, for example by holding D.
  if (dockStandby_ && powerMode_ != PowerMode::Standby) dockStandby_ = false;
  serviceDockPowerOff(now);
}

// The charger was plugged in. A device on energy saving standby wakes for it. With dock mode on, the device
// goes to standby after kDockPowerOffDelayMs, so the charging card shows first.
void InventatoryScanApp::onPlugged(unsigned long now) {
  Serial.println("Charger input plugged in.");
  if (powerMode_ == PowerMode::Standby) exitStandby();
  if (!dockMode_) return;
  Serial.println("Dock mode: standby shortly while on the charger.");
  dockPowerOffPending_ = true;
  dockPowerOffAt_ = now + kDockPowerOffDelayMs;
}

// The charger was taken off. Dock standby ends at once, and the device comes back on. A standby that was
// scheduled and not yet started is dropped.
void InventatoryScanApp::onUnplugged() {
  Serial.println("Charger input unplugged.");
  dockPowerOffPending_ = false;
  if (!dockStandby_) return;
  dockStandby_ = false;
  exitStandby();
}

// Dock mode's standby. It starts once the charge card has shown, and only if the device is idle and still on
// the charger. Otherwise the device stays on.
void InventatoryScanApp::serviceDockPowerOff(unsigned long now) {
  if (!dockPowerOffPending_ || static_cast<long>(now - dockPowerOffAt_) < 0) return;
  dockPowerOffPending_ = false;
  if (!dockMode_ || !chargerPlugged_ || !canShowBatteryCard()) {
    Serial.println("Dock mode: standby skipped, the device is in use or no longer on the charger.");
    return;
  }
  dockStandby_ = true;
  enterStandby();
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
  // On the charger the device stays awake, so energy saving never sleeps it. Dock mode turns it off instead.
  in.idleReady = powerMode_ == PowerMode::Normal && state_ == State::Idle && !setupMode_ &&
                 !lowBatteryWarning_ && !powerOffWarning_ && !display_.errorVisible() && !chargerPlugged_;
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
