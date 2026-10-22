// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#include "app/inventatory_scan_app.h"

#include "InventatoryScanCore.h"
#include "InventatoryScanErrorCodes.h"
#include "InventatoryScanUiPolicy.h"
#include "config/config.h"
#include "config/version.h"
#include "log/device_log.h"

namespace inventatory_scan {

namespace {

// Moves a setting along its options, stopping at the first and the last.
template <typename Option>
Option stepOption(Option current, int step, int count) {
  return static_cast<Option>(constrain(static_cast<int>(current) + step, 0, count - 1));
}

}  // namespace

void InventatoryScanApp::enterStandby() {
  if (powerMode_ == PowerMode::Standby) {
    return;
  }

  deviceLog.println("Entering standby mode.");
  resetPending();
  otaRequested_ = false;
  otaActive_ = false;
  lastReconnectAttempt_ = 0;
  lastSyncAttempt_ = 0;
  wifiConnectedReported_ = false;

  networkPowerOffRequested_ = true;
  networkPaused_ = true;
  scanner_.suspend();
  powerMode_ = PowerMode::Standby;
  pcOnline_ = false;
  display_.setStandby(true);
  deviceLog.println("Standby mode active. Hold D again to wake.");
  sendDebugMessage("standby entered", "warn");
}

void InventatoryScanApp::exitStandby() {
  if (powerMode_ != PowerMode::Standby) {
    return;
  }

  deviceLog.println("Waking from standby mode.");
  // Waking from an energy-saving sleep restarts the sequence.
  if (energyPhase_ == EnergyPhase::Asleep) energyPhase_ = EnergyPhase::Awake;
  const bool scannerReady = scanner_.resume();
  networkPowerOffRequested_ = false;
  networkPaused_ = false;
  wifiConnectedReported_ = false;
  lastReconnectAttempt_ = 0;
  lastSyncAttempt_ = 0;
  powerMode_ = PowerMode::Normal;
  display_.setStandby(false);

  if (!scannerReady) {
    deviceLog.println("Scanner resume failed; verify the GM65 connection and profile.");
    display_.showLocalFailure(localFailure("local-scanner", kScannerFaultCode, "Check scanner cable"));
  }

  if (provisionedSettings_.wifiSsid.length() == 0) {
    deviceLog.println("Standby exit complete; Wi-Fi remains offline.");
  }
  sendDebugMessage("standby exited", "warn");
}

void InventatoryScanApp::logState(const char* reason) const {
  deviceLog.print("State update [");
  deviceLog.print(reason);
  deviceLog.print("]: ");
  deviceLog.println(state_ == State::Idle ? "idle" : state_ == State::AwaitQuantity ? "awaiting quantity" : "other");
}

void InventatoryScanApp::openQuickLabels() {
  state_ = State::QuickLabels;
  if (quickLabelPage_ * kQuickLabelsPerPage >= quickLabelPresetCount_) quickLabelPage_ = 0;
  quickLabelSnapshotReceived_ = false;
  pcSupportsQuickLabels_ = false;
  display_.showQuickLabels(quickLabelPresets_, quickLabelPresetCount_, quickLabelPage_,
                           quickLabelSnapshotReceived_, pcSupportsQuickLabels_);
  refreshQuickLabels();
}

void InventatoryScanApp::openQuickSettings() {
  state_ = State::QuickSettings;
  display_.showQuickSettings();
}

void InventatoryScanApp::openDisplaySettings() {
  state_ = State::DisplaySettings;
  display_.showDisplaySettings(lcdContrast_, String(backlightModeShortLabel(backlightMode_)));
}

void InventatoryScanApp::openBacklightSettings() {
  state_ = State::AdjustBacklight;
  display_.showBacklight(String(backlightModeLabel(backlightMode_)), String(backlightModeDetail(backlightMode_)),
                         static_cast<std::size_t>(backlightMode_), kBacklightModeCount);
}

// A and B step the list; the change applies and saves at once.
void InventatoryScanApp::stepBacklightMode(int step) {
  backlightMode_ = stepOption(backlightMode_, step, kBacklightModeCount);
  persistBacklightMode();
  noteUserActivity();
  openBacklightSettings();
}

void InventatoryScanApp::persistBacklightMode() {
  if (!deviceSettings_.saveBacklightMode(static_cast<uint8_t>(backlightMode_))) {
    deviceLog.println("Could not save backlight mode.");
  }
}

void InventatoryScanApp::openEnergySaving() {
  state_ = State::EnergySaving;
  display_.showEnergySaving(energySavingOn_, String(sleepDelayShortLabel(sleepDelay_)),
                            String(powerOffDelayShortLabel(powerOffDelay_)), dockMode_, energyPage_,
                            String(batteryCapacity_) + " mAh");
}

// D moves to the next page of the energy list, as D does on Quick labels.
void InventatoryScanApp::cycleEnergyPage() {
  energyPage_ = static_cast<uint8_t>((energyPage_ + 1U) % kEnergyPageCount);
  openEnergySaving();
}

// The capacity is typed in mAh. Nothing changes until A saves it, and C leaves it as it was.
void InventatoryScanApp::openBatteryCapacityEntry() {
  state_ = State::AwaitBatteryCapacity;
  batteryEntryMah_ = 0;
  refreshBatteryCapacityEntry(false);
}

void InventatoryScanApp::refreshBatteryCapacityEntry(bool rejected) {
  String value = "Type mAh";
  String detail = "Now " + String(batteryCapacity_) + " mAh";
  if (batteryEntryMah_ > 0) {
    value = String(batteryEntryMah_) + " mAh";
    detail = "# deletes a digit";
  }
  if (rejected) detail = "Use " + String(kBatteryCapacityMinMah) + " to " + String(kBatteryCapacityMaxMah) + " mAh";
  display_.showBatteryCapacityEntry(value, detail);
}

void InventatoryScanApp::appendBatteryCapacityDigit(char digit) {
  const uint32_t next = static_cast<uint32_t>(batteryEntryMah_) * 10U + static_cast<uint32_t>(digit - '0');
  if (next > kBatteryCapacityMaxMah) return;
  batteryEntryMah_ = static_cast<uint16_t>(next);
  refreshBatteryCapacityEntry(false);
}

void InventatoryScanApp::deleteBatteryCapacityDigit() {
  batteryEntryMah_ = static_cast<uint16_t>(batteryEntryMah_ / 10U);
  refreshBatteryCapacityEntry(false);
}

// A saves the typed capacity. Nothing typed goes back with no change, and a value outside the range stays on screen.
void InventatoryScanApp::confirmBatteryCapacity() {
  if (batteryEntryMah_ == 0) {
    openEnergySaving();
    return;
  }
  if (!batteryCapacityAccepted(batteryEntryMah_)) {
    refreshBatteryCapacityEntry(true);
    return;
  }
  batteryCapacity_ = batteryEntryMah_;
  batteryTracker_.setCapacity(batteryCapacityUah(batteryCapacity_));
  persistBatteryCapacity();
  noteUserActivity();
  openEnergySaving();
}

void InventatoryScanApp::persistBatteryCapacity() {
  if (!deviceSettings_.saveBatteryCapacity(batteryCapacity_)) deviceLog.println("Could not save battery capacity.");
}

// Delays are kept while energy saving is off.
void InventatoryScanApp::toggleEnergySaving() {
  energySavingOn_ = !energySavingOn_;
  persistEnergySaving();
  noteUserActivity();
  openEnergySaving();
}

void InventatoryScanApp::persistEnergySaving() {
  if (!deviceSettings_.saveEnergySavingEnabled(energySavingOn_)) deviceLog.println("Could not save energy saving.");
}

// Independent of energy saving.
void InventatoryScanApp::toggleDockMode() {
  dockMode_ = !dockMode_;
  persistDockMode();
  noteUserActivity();
  openEnergySaving();
}

void InventatoryScanApp::persistDockMode() {
  if (!deviceSettings_.saveDockModeEnabled(dockMode_)) deviceLog.println("Could not save dock mode.");
}

void InventatoryScanApp::openSleepAfterSettings() {
  state_ = State::AdjustSleepAfter;
  display_.showSleepAfter(String(sleepDelayLabel(sleepDelay_)), String(sleepDelayDetail(sleepDelay_)),
                          static_cast<std::size_t>(sleepDelay_), kSleepDelayCount);
}

// A and B step the list, as for the backlight.
void InventatoryScanApp::stepSleepDelay(int step) {
  sleepDelay_ = stepOption(sleepDelay_, step, kSleepDelayCount);
  persistSleepDelay();
  noteUserActivity();
  openSleepAfterSettings();
}

void InventatoryScanApp::persistSleepDelay() {
  if (!deviceSettings_.saveSleepDelay(static_cast<uint8_t>(sleepDelay_))) deviceLog.println("Could not save sleep delay.");
}

void InventatoryScanApp::openPowerOffSettings() {
  state_ = State::AdjustPowerOff;
  display_.showPowerOffAfter(String(powerOffDelayLabel(powerOffDelay_)), String(powerOffDelayDetail(powerOffDelay_)),
                             static_cast<std::size_t>(powerOffDelay_), kPowerOffDelayCount);
}

// A steps towards the shortest delay, B towards Never.
void InventatoryScanApp::stepPowerOffDelay(int step) {
  powerOffDelay_ = stepOption(powerOffDelay_, step, kPowerOffDelayCount);
  persistPowerOffDelay();
  noteUserActivity();
  openPowerOffSettings();
}

void InventatoryScanApp::persistPowerOffDelay() {
  if (!deviceSettings_.savePowerOffDelay(static_cast<uint8_t>(powerOffDelay_))) {
    deviceLog.println("Could not save power off delay.");
  }
}

// Pairing and warnings keep the backlight lit whatever the setting; standby keeps it dark.
void InventatoryScanApp::serviceBacklight() {
  const bool forced = lowBatteryWarning_ || setupMode_ || otaRequested_ || otaActive_ || energyWarningShown();
  const bool lit = powerMode_ != PowerMode::Standby &&
                   backlightLit(backlightMode_, forced, millis(), lastUserActivityAt_);
  backlight_.setLit(lit);
}

void InventatoryScanApp::refreshQuickLabels() {
  lastSyncAttempt_ = 0;
}

void InventatoryScanApp::requestQuickLabelPrint(uint8_t presetIndex) {
  if (!quickLabelSnapshotReceived_ || !pcSupportsQuickLabels_ || presetIndex < 1 ||
      presetIndex > quickLabelPresetCount_ || quickLabelRevision_ == 0) {
    display_.showQuickLabelPrintResult(false, "REFRESH PRESETS FIRST", kPresetsNotLoadedCode);
    state_ = State::Idle;
    return;
  }
  if (!WiFi.isConnected()) {
    display_.showQuickLabelPrintResult(false, "PC OFFLINE", kPrintPcOfflineCode);
    state_ = State::Idle;
    return;
  }
  // Wi-Fi is connected here, so the radio feeds the random number generator. Zero means not drawn yet.
  while (bootNonce_ == 0) bootNonce_ = esp_random() & 0xFFFFFFUL;
  pendingQuickLabelPrintId_ = clientConfig_.deviceId + String("-label-") + String(bootNonce_, HEX) + String("-") +
                                String(syncSequence_++);
  pendingQuickLabelPrintStartedAt_ = millis();
  pendingQuickLabelPresetIndex_ = presetIndex;
  pendingQuickLabelRevision_ = quickLabelRevision_;
  state_ = State::PrintingLabel;
  display_.showPrinting(quickLabelPresets_[presetIndex - 1]);
  lastSyncAttempt_ = 0;
}

void InventatoryScanApp::persistContrast() {
  if (!deviceSettings_.saveContrast(lcdContrast_)) deviceLog.println("Could not save LCD contrast.");
}

DisplayConnectivityState InventatoryScanApp::connectivityState() const {
  if (!WiFi.isConnected()) {
    if (provisionedSettings_.wifiSsid.length() > 0 && powerMode_ == PowerMode::Normal) {
      return DisplayConnectivityState::WifiConnecting;
    }
    return DisplayConnectivityState::Offline;
  }
  if (!pcOnline_) return DisplayConnectivityState::PcSearching;
  if (!outbox_.empty() || pendingResultAck_.length() > 0 || activeLookupId_.length() > 0 ||
      pendingQuickLabelPrintId_.length() > 0) {
    return DisplayConnectivityState::Syncing;
  }
  return DisplayConnectivityState::Online;
}

// Off on purpose in standby. Otherwise faulted if the boot profile was not acknowledged,
// or if it did not wake.
bool InventatoryScanApp::scannerFaulted() const {
  return scannerFaultShown(powerMode_ == PowerMode::Standby, scanner_.active());
}

void InventatoryScanApp::updateConnectivityDisplay() {
  display_.setQueueUnreadable(outbox_.unreadable());
  const bool wifiOnline = WiFi.isConnected();
  display_.setConnectivity(connectivityState(), outbox_.size());
  if (state_ == State::AwaitQuantity && activeLookupId_.length() > 0) {
    display_.updateQuantityLookup(wifiOnline ? String("Checking inventory") : String("Waiting for PC"),
                                  wifiOnline ? QuantityLookupState::Pending
                                             : QuantityLookupState::Unavailable);
  }
}

}  // namespace inventatory_scan
