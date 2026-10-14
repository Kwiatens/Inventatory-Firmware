// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Inventatory Scan R1 application controller implementation.

#include "app/inventatory_scan_app.h"

#include "InventatoryScanCore.h"
#include "InventatoryScanErrorCodes.h"
#include "InventatoryScanUiPolicy.h"
#include "config/config.h"
#include "config/version.h"

namespace inventatory_scan {

void InventatoryScanApp::enterStandby() {
  if (powerMode_ == PowerMode::Standby) {
    return;
  }

  Serial.println("Entering standby mode.");
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
  Serial.println("Standby mode active. Hold D again to wake.");
  sendDebugMessage("standby entered", "warn");
}

void InventatoryScanApp::exitStandby() {
  if (powerMode_ != PowerMode::Standby) {
    return;
  }

  Serial.println("Waking from standby mode.");
  const bool scannerReady = scanner_.resume();
  networkPowerOffRequested_ = false;
  networkPaused_ = false;
  wifiConnectedReported_ = false;
  lastReconnectAttempt_ = 0;
  lastSyncAttempt_ = 0;
  powerMode_ = PowerMode::Normal;
  display_.setStandby(false);

  if (!scannerReady) {
    Serial.println("Scanner resume failed; verify the GM65 connection and profile.");
    SyncResult result;
    result.resultId = "local-scanner";
    result.eventId = "local-scanner";
    result.status = "failed";
    result.code = kScannerFaultCode;
    result.message = "Check scanner cable";
    display_.showLocalFailure(result);
  }

  if (provisionedSettings_.wifiSsid.length() == 0) {
    Serial.println("Standby exit complete; Wi-Fi remains offline.");
  }
  sendDebugMessage("standby exited", "warn");
}

void InventatoryScanApp::logState(const char* reason) const {
  Serial.print("State update [");
  Serial.print(reason);
  Serial.print("]: ");
  Serial.println(state_ == State::Idle ? "idle" : state_ == State::AwaitQuantity ? "awaiting quantity" : "menu");
}

void InventatoryScanApp::openQuickLabels() {
  state_ = State::QuickLabels;
  if (quickLabelPage_ * 4U >= quickLabelPresetCount_) quickLabelPage_ = 0;
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

// A and B move along the list. The change takes effect at once and is saved, like the contrast.
void InventatoryScanApp::stepBacklightMode(int step) {
  const int current = static_cast<int>(backlightMode_);
  const int next = constrain(current + step, 0, static_cast<int>(kBacklightModeCount) - 1);
  backlightMode_ = static_cast<BacklightMode>(next);
  persistBacklightMode();
  noteUserActivity();
  openBacklightSettings();
}

void InventatoryScanApp::persistBacklightMode() {
  if (!deviceSettings_.saveBacklightMode(static_cast<uint8_t>(backlightMode_))) {
    Serial.println("Could not save backlight mode.");
  }
}

// The backlight stays lit for pairing and warnings whatever the setting says, and
// stays dark in standby, where the panel itself is off.
void InventatoryScanApp::serviceBacklight() {
  const bool forced = lowBatteryWarning_ || setupMode_ || otaRequested_ || otaActive_;
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
  if (!deviceSettings_.saveContrast(lcdContrast_)) Serial.println("Could not save LCD contrast.");
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

// The scanner is off on purpose in standby. Otherwise it is faulted when its boot profile was not
// acknowledged, or when it did not wake.
bool InventatoryScanApp::scannerFaulted() const {
  return scannerFaultShown(powerMode_ == PowerMode::Standby, scanner_.active());
}

void InventatoryScanApp::updateConnectivityDisplay() {
  display_.setQueueUnreadable(outbox_.unreadable());
  const bool wifiOnline = WiFi.isConnected();
  display_.setConnectivity(connectivityState(), wifiOnline, pcOnline_, outbox_.size());
  if (state_ == State::AwaitQuantity && activeLookupId_.length() > 0) {
    display_.updateQuantityLookup(wifiOnline ? String("Checking inventory") : String("Waiting for PC"),
                                  wifiOnline ? QuantityLookupState::Pending
                                             : QuantityLookupState::Unavailable);
  }
}

}  // namespace inventatory_scan
