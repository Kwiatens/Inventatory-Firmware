// Inventatory Scan R1 application controller implementation.

#include "app/inventatory_scan_app.h"

#include "InventatoryScanCore.h"
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
    result.code = "SCANNER ERROR";
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
  display_.showQuickSettings(lcdContrast_);
}

void InventatoryScanApp::refreshQuickLabels() {
  lastSyncAttempt_ = 0;
}

void InventatoryScanApp::requestQuickLabelPrint(uint8_t presetIndex) {
  if (!quickLabelSnapshotReceived_ || !pcSupportsQuickLabels_ || presetIndex < 1 ||
      presetIndex > quickLabelPresetCount_ || quickLabelRevision_ == 0) {
    display_.showQuickLabelPrintResult(false, "REFRESH PRESETS FIRST");
    state_ = State::Idle;
    return;
  }
  if (!WiFi.isConnected()) {
    display_.showQuickLabelPrintResult(false, "PC OFFLINE");
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

void InventatoryScanApp::updateConnectivityDisplay() {
  const bool wifiOnline = WiFi.isConnected();
  display_.setConnectivity(connectivityState(), wifiOnline, pcOnline_, outbox_.size());
  if (state_ == State::AwaitQuantity && activeLookupId_.length() > 0) {
    display_.updateQuantityLookup(wifiOnline ? String("Checking inventory") : String("Waiting for PC"),
                                  wifiOnline ? QuantityLookupState::Pending
                                             : QuantityLookupState::Unavailable);
  }
}

}  // namespace inventatory_scan
