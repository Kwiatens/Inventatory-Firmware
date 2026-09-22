// Inventatory Scan R1 application controller implementation.

#include "app/inventatory_scan_app.h"

#include "InventatoryScanCore.h"
#include "InventatoryScanUiPolicy.h"
#include "config/config.h"
#include "config/version.h"

#include <cctype>

namespace inventatory_scan {

namespace {

bool isQuantityCode(const String& code) {
  const auto trimmed = trimCopy(code);
  if (trimmed.length() == 0) {
    return false;
  }
  for (size_t index = 0; index < trimmed.length(); ++index) {
    if (!std::isdigit(static_cast<unsigned char>(trimmed[index]))) {
      return false;
    }
  }
  return true;
}

bool hasAimPrefix(const String& value, char symbology) {
  return value.length() >= 2U && value[0] == ']' &&
         std::tolower(static_cast<unsigned char>(value[1])) == std::tolower(static_cast<unsigned char>(symbology));
}

bool hasIso15434Macro06Envelope(const String& value) {
  if (!value.startsWith("[)>")) {
    return false;
  }

  if (value.length() >= 6U && static_cast<unsigned char>(value[3]) == 0x1EU && value[4] == '0' && value[5] == '6') {
    return true;
  }

  // Some scanner profiles suppress RS/GS control characters. A component Macro
  // 06 payload can then arrive as "[)>06P..." rather than "[)><RS>06<GS>P...".
  return value.length() >= 5U && value[3] == '0' && value[4] == '6';
}

bool isDataMatrixScan(const String& value) {
  return hasAimPrefix(value, 'd') || hasIso15434Macro06Envelope(stripAimSymbologyPrefix(value));
}

bool isQrScan(const String& value) {
  return hasAimPrefix(value, 'q');
}


}  // namespace

void InventatoryScanApp::pollScanner() {
  if (state_ != State::WaitingForResult && deferredScan_.length() > 0) {
    const auto code = deferredScan_;
    deferredScan_.clear();
    handleScan(code);
    return;
  }
  String code;
  if (scanner_.poll(code)) {
    // Preserve one completed scan while an inventory result is pending. Stop
    // draining after this so subsequent scanner input remains subject to the
    // UART's fixed bound instead of silently replacing the deferred scan.
    if (state_ == State::WaitingForResult) {
      if (deferredScan_.length() == 0) deferredScan_ = code;
      return;
    }
    handleScan(code);
  }
}

void InventatoryScanApp::handleScan(const String& code) {
  if (!scanMayInterruptUi(setupMode_, powerMode_ == PowerMode::Standby,
                          otaRequested_ || otaActive_, state_ == State::ConfirmReprovision,
                          state_ == State::PrintingLabel)) {
    Serial.println("Scan ignored while the current operation blocks scanning.");
    return;
  }
  if (state_ != State::Idle) resetPending();
  // Some GM65 profiles prepend an AIM symbology identifier (for example ]C1
  // for Code 128, ]Q3 for QR, or ]d2 for Data Matrix). We use that marker to
  // decode standards-based Data Matrix fields and route QR scans to the
  // Inventatory machine-code path.
  const auto raw = trimCopy(code);
  const auto trimmed = trimCopy(stripAimSymbologyPrefix(raw));
  if (trimmed.length() == 0) {
    return;
  }

  if (isDataMatrixScan(raw)) {
    DecodedComponentFields decoded;
    if (!decodeComponentDataMatrix(raw, decoded)) {
      Serial.println("Rejected non-standard Data Matrix scan.");
      return;
    }
    const auto& lookupCode = decoded.manufacturerPartNumber;
    const auto& manufacturer = decoded.manufacturer;
    const auto& encodedPartName = decoded.encodedPartName;
    const auto forwardedCode = lookupCode.length() > 0 ? lookupCode : encodedPartName;
    const auto defaultQuantity = decoded.quantity;
    resetPending();
    scannedCode_ = forwardedCode;
    scannedManufacturer_ = manufacturer;
    scannedManufacturerPartNumber_ = lookupCode;
    scannedPartName_ = encodedPartName;
    quantity_.setDefaultValue(defaultQuantity);
    pendingScanType_ = PendingScanType::ComponentDataMatrix;
    activeLookupCode_ = forwardedCode;
    activeLookupId_ = clientConfig_.deviceId + String("-lookup-") + String(lookupSequence_++);
    lookupMissCount_ = 0;
    setAwaitingQuantity();
    display_.showQuantity("Checking inventory", String("Part ") + scannedCode_,
                          quantity_.displayText(), quantity_.cursor(), true,
                          QuantityLookupState::Pending);
    lastSyncAttempt_ = 0;
    Serial.print("Decoded default quantity: ");
    Serial.println(quantity_.displayText().c_str());
    Serial.println("Enter quantity, then press A to add. C cancels.");
    sendDebugMessage("data matrix scan pending", "scan");
    return;
  }

  if (isQrScan(raw) || isQuantityCode(trimmed)) {
    if (!isStandardMachineCode(trimmed)) {
      Serial.println("Rejected non-standard inventory ID scan.");
      return;
    }
    resetPending();
    scannedCode_ = trimmed;
    quantity_.setDefaultValue(1);
    pendingScanType_ = PendingScanType::InventatoryQuantity;
    activeLookupCode_ = trimmed;
    activeLookupId_ = clientConfig_.deviceId + String("-lookup-") + String(lookupSequence_++);
    lookupMissCount_ = 0;
    setAwaitingQuantity();
    display_.showQuantity("Checking inventory", String("Code ") + trimmed,
                          quantity_.displayText(), quantity_.cursor(), false,
                          QuantityLookupState::Pending);
    lastSyncAttempt_ = 0;
    Serial.println("Enter quantity, then press A to add or B to subtract. C cancels.");
    sendDebugMessage("inventory scan pending", "scan");
    return;
  }

  Serial.println("Rejected unrecognized scanner data.");
}

void InventatoryScanApp::handleKey(const InventatoryKeyEvent& event) {
  if (event.type == InventatoryKeyEventType::ProvisioningReset) {
    networkPowerOffRequested_ = false;
    networkPaused_ = true;
    const auto pauseDeadline = millis() + 1000UL;
    while (networkStarted_ && !networkPauseAcknowledged_ &&
           static_cast<long>(pauseDeadline - millis()) > 0) {
      delay(1);
    }
    if (networkStarted_ && !networkPauseAcknowledged_) {
      networkPaused_ = false;
      Serial.println("Provisioning reset deferred because network storage is busy.");
      return;
    }
    if (setupStore_.clear()) {
      Serial.println("Provisioning reset requested; restarting into BLE setup.");
      ESP.restart();
    }
    networkPaused_ = false;
    Serial.println("Provisioning reset could not clear NVS.");
    return;
  }
  if (event.type == InventatoryKeyEventType::OtaUpdate) {
    requestOtaUpdate();
    return;
  }

  if (event.type == InventatoryKeyEventType::PowerToggle) {
    if (state_ != State::Idle) return;
    if (powerMode_ == PowerMode::Standby) {
      exitStandby();
    } else {
      enterStandby();
    }
    return;
  }

  if (state_ == State::WaitingForResult) {
    if (event.type == InventatoryKeyEventType::Cancel) {
      Serial.println("Waiting for Inventatory to confirm the submitted change.");
    }
    return;
  }

  if (event.type == InventatoryKeyEventType::Digit) {
    if (state_ == State::Menu) {
      if (event.value == '1') openQuickLabels();
      else if (event.value == '2') openQuickSettings();
      return;
    }
    if (state_ == State::QuickLabels) {
      const uint8_t selected = static_cast<uint8_t>(event.value - '0');
      if (selected >= 1 && selected <= 4) requestQuickLabelPrint(quickLabelPage_ * 4U + selected);
      return;
    }
    if (state_ == State::QuickSettings) {
      if (event.value == '1') {
        state_ = State::AdjustContrast;
        display_.showContrast(lcdContrast_);
      } else if (event.value == '2') {
        state_ = State::Idle;
        enterStandby();
      } else if (event.value == '3') {
        state_ = State::ConfirmReprovision;
        display_.showResetConfirm();
      } else if (event.value == '4') {
        state_ = State::Idle;
        requestOtaUpdate();
      }
      return;
    }
    if (state_ == State::Idle) {
      Serial.println("Digit ignored, scan a code first.");
      return;
    }
    quantity_.appendDigit(event.value);
    display_.updateQuantity(quantity_.displayText(), quantity_.cursor());
    Serial.print("Quantity: ");
    Serial.println(quantity_.displayText().c_str());
    return;
  }

  if (event.type == InventatoryKeyEventType::CycleCursor) {
    if (state_ == State::QuickLabels && quickLabelPresetCount_ > 4U) {
      const uint8_t pages = static_cast<uint8_t>((quickLabelPresetCount_ + 3U) / 4U);
      quickLabelPage_ = static_cast<uint8_t>((quickLabelPage_ + 1U) % pages);
      display_.showQuickLabels(quickLabelPresets_, quickLabelPresetCount_, quickLabelPage_,
                               quickLabelSnapshotReceived_, pcSupportsQuickLabels_);
      return;
    }
    if (state_ == State::AwaitQuantity) {
      quantity_.cycleCursor();
      display_.updateQuantity(quantity_.displayText(), quantity_.cursor());
    }
    return;
  }

  if (event.type == InventatoryKeyEventType::DeleteDigit) {
    if (state_ == State::AwaitQuantity) {
      quantity_.deleteDigit();
      display_.updateQuantity(quantity_.displayText(), quantity_.cursor());
      Serial.print("Quantity: ");
      Serial.println(quantity_.displayText().c_str());
    }
    return;
  }

  if (event.type == InventatoryKeyEventType::Cancel) {
    if (state_ == State::Idle) {
      if (display_.errorVisible()) display_.dismissError();
      else {
        Serial.println("Opening device menu.");
        state_ = State::Menu;
        display_.showMenu();
      }
      return;
    }
    if (state_ == State::Menu || state_ == State::QuickLabels || state_ == State::QuickSettings ||
        state_ == State::PrintingLabel) {
      pendingQuickLabelPrintId_.clear();
      pendingQuickLabelPresetIndex_ = 0;
      state_ = State::Idle;
      display_.showReady();
      return;
    }
    if (state_ == State::AdjustContrast) {
      persistContrast();
      openQuickSettings();
      return;
    }
    if (state_ == State::ConfirmReprovision) {
      openQuickSettings();
      return;
    }
    resetPending();
    display_.finishInteraction();
    Serial.println("Cancelled pending scan.");
    return;
  }

  if (event.type == InventatoryKeyEventType::Add || event.type == InventatoryKeyEventType::Subtract) {
    if (state_ == State::AdjustContrast) {
      if (event.type == InventatoryKeyEventType::Add && lcdContrast_ > 0) --lcdContrast_;
      if (event.type == InventatoryKeyEventType::Subtract && lcdContrast_ < 63) ++lcdContrast_;
      display_.setContrast(lcdContrast_);
      persistContrast();
      display_.showContrast(lcdContrast_);
      return;
    }
    if (state_ == State::ConfirmReprovision && event.type == InventatoryKeyEventType::Add) {
      if (setupStore_.clear()) {
        Serial.println("Re-pair requested from quick settings; restarting BLE setup.");
        ESP.restart();
      }
      display_.showQuickLabelPrintResult(false, "RESET FAILED");
      state_ = State::Idle;
      return;
    }
    if (state_ == State::Idle || scannedCode_.length() == 0) {
      Serial.println("No scanned code to submit.");
      return;
    }
    if (pendingScanType_ == PendingScanType::ComponentDataMatrix && event.type == InventatoryKeyEventType::Subtract) {
      Serial.println("Component scans use A to confirm the quantity.");
      return;
    }
    submitCurrent(event.type == InventatoryKeyEventType::Add ? 'A' : 'B');
  }
}

void InventatoryScanApp::submitCurrent(char action) {
  if (pendingScanType_ == PendingScanType::ComponentDataMatrix) {
    if (action != 'A') {
      Serial.println("Component scans require A to confirm.");
      return;
    }

    const auto quantity = quantity_.consume(true);
    String eventId;
    const auto queued = queueEvent(DeviceEventKind::InventoryReceive, scannedCode_, quantity, &eventId,
                                   scannedManufacturer_, scannedManufacturerPartNumber_,
                                   scannedPartName_);
    Serial.print("Queued decoded-component quantity ");
    Serial.println(String(quantity).c_str());
    resetPending();
    if (!queued) {
      display_.finishInteraction();
      return;
    }
    waitForInventoryResult(eventId);
    return;
  }

  const auto delta = quantity_.consume(action == 'A');
  String eventId;
  const auto queued = queueEvent(DeviceEventKind::InventoryAdjust, scannedCode_, delta, &eventId);
  resetPending();
  if (!queued) {
    display_.finishInteraction();
    return;
  }
  waitForInventoryResult(eventId);
}

bool InventatoryScanApp::queueEvent(DeviceEventKind kind, const String& code, int value, String* eventId,
                                    const String& manufacturer, const String& manufacturerPartNumber,
                                    const String& encodedPartName) {
  DeviceEventRecord event;
  const auto queueResult =
      outbox_.enqueue(kind, code, value, &event, manufacturer, manufacturerPartNumber, encodedPartName);
  if (queueResult != OutboxStore::EnqueueResult::Queued) {
    SyncResult result;
    result.resultId = "local-queue";
    result.eventId = "local-queue";
    result.status = "failed";
    switch (queueResult) {
      case OutboxStore::EnqueueResult::QueueFull:
        result.code = "QUEUE FULL";
        result.message = "Sync PC before scanning";
        break;
      case OutboxStore::EnqueueResult::StorageFailed:
        result.code = "STORAGE ERROR";
        result.message = "Scan storage write failed";
        break;
      default:
        result.code = "SCAN ERROR";
        result.message = "Scan data cannot be saved";
        break;
    }
    display_.showResult(result);
    Serial.println(result.message.c_str());
    return false;
  }
  if (eventId != nullptr) *eventId = event.eventId;
  Serial.println("Queued protocol v1 inventory event.");
  lastSyncAttempt_ = 0;
  return true;
}

void InventatoryScanApp::waitForInventoryResult(const String& eventId) {
  awaitingResultEventId_ = eventId;
  state_ = State::WaitingForResult;
  display_.showSending();
}

void InventatoryScanApp::setAwaitingQuantity() {
  state_ = State::AwaitQuantity;
}

void InventatoryScanApp::resetPending() {
  scannedCode_.clear();
  scannedManufacturer_.clear();
  scannedManufacturerPartNumber_.clear();
  scannedPartName_.clear();
  quantity_.clear();
  activeLookupId_.clear();
  activeLookupCode_.clear();
  lookupMissCount_ = 0;
  state_ = State::Idle;
  pendingScanType_ = PendingScanType::None;
  pendingQuickLabelPrintId_.clear();
  pendingQuickLabelPresetIndex_ = 0;
  pendingQuickLabelRevision_ = 0;
}

}  // namespace inventatory_scan