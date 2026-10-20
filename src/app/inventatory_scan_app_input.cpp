// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#include "app/inventatory_scan_app.h"

#include "InventatoryScanCore.h"
#include "InventatoryScanErrorCodes.h"
#include "InventatoryScanUiPolicy.h"
#include "config/config.h"
#include "config/version.h"
#include "log/device_log.h"

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

  // Some profiles strip RS/GS, so Macro 06 can arrive as "[)>06P..." without them.
  return value.length() >= 5U && value[3] == '0' && value[4] == '6';
}

bool isDataMatrixScan(const String& value) {
  return hasAimPrefix(value, 'd') || hasIso15434Macro06Envelope(stripAimSymbologyPrefix(value));
}

bool isQrScan(const String& value) {
  return hasAimPrefix(value, 'q');
}

// Short presses wake a device asleep on its energy saving timer and dismiss a countdown warning.
// Holds keep their meaning: '*' warns and powers off, D toggles standby,
// '*' with D starts an update.
bool isShortKeyPress(InventatoryKeyEventType type) {
  return type == InventatoryKeyEventType::Digit || type == InventatoryKeyEventType::Add ||
         type == InventatoryKeyEventType::Subtract || type == InventatoryKeyEventType::Cancel ||
         type == InventatoryKeyEventType::CycleCursor || type == InventatoryKeyEventType::DeleteDigit;
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
    // Keep one scan while a result is pending. The scanner is still drained so UART bytes cannot
    // overflow. A further scan is reported, not silently lost.
    if (state_ == State::WaitingForResult) {
      if (deferredScan_.length() == 0) {
        deferredScan_ = code;
      } else {
        deviceLog.println("Scan ignored: a previous scan is already waiting for the result.");
      }
      return;
    }
    handleScan(code);
  }
}

void InventatoryScanApp::handleScan(const String& code) {
  if (lowBatteryWarning_) {
    deviceLog.println("Scan ignored while the battery warning is shown.");
    return;
  }
  noteUserActivity();
  // A scan dismisses a countdown warning, then is handled as usual.
  dismissEnergyWarning();
  if (!scanMayInterruptUi(setupMode_, powerMode_ == PowerMode::Standby,
                          otaRequested_ || otaActive_,
                          state_ == State::ConfirmReprovision || state_ == State::ConfirmDiscard || state_ == State::ConfirmRepair,
                          state_ == State::PrintingLabel)) {
    deviceLog.println("Scan ignored while the current operation blocks scanning.");
    return;
  }
  // Reset pending state only once a scan is accepted, so a rejected frame cannot desync the screen.
  // Some GM65 profiles prepend an AIM identifier (]C1, ]Q3 QR, ]d2 Data Matrix). It selects
  // the Data Matrix decoder and the QR path.
  const auto raw = trimCopy(code);
  const auto trimmed = trimCopy(stripAimSymbologyPrefix(raw));
  if (trimmed.length() == 0) {
    return;
  }

  if (isDataMatrixScan(raw)) {
    DecodedComponentFields decoded;
    if (!decodeComponentDataMatrix(raw, decoded)) {
      deviceLog.println("Rejected non-standard Data Matrix scan.");
      return;
    }
    const auto& lookupCode = decoded.manufacturerPartNumber;
    const auto& manufacturer = decoded.manufacturer;
    const auto& encodedPartName = decoded.encodedPartName;
    // The desktop recognises bags by DigiKey part number, so refuse a Data Matrix without one.
    const auto bagCode = digiKeyBagCodeForSync(decoded.auxiliaryPartNumber, lookupCode, kMaximumBagEventCodeBytes);
    if (bagCode.length() == 0) {
      showScanRefusal("Not a supported vendor bag. Add the part by hand",
                      "Rejected Data Matrix scan that no supported vendor bag carries.");
      return;
    }
    const auto forwardedCode = lookupCode.length() > 0 ? lookupCode : encodedPartName;
    const auto defaultQuantity = decoded.quantity;
    resetPending();
    scannedCode_ = forwardedCode;
    scannedEventCode_ = bagCode;
    scannedManufacturer_ = manufacturer;
    scannedManufacturerPartNumber_ = lookupCode;
    scannedPartName_ = encodedPartName;
    quantity_.setDefaultValue(defaultQuantity);
    pendingScanType_ = PendingScanType::ComponentDataMatrix;
    // Look up by DigiKey part number (the desktop matches it); manufacturer number is display only.
    activeLookupCode_ = trimCopy(decoded.auxiliaryPartNumber);
    activeLookupId_ = clientConfig_.deviceId + String("-lookup-") + String(lookupSequence_++);
    lookupMissCount_ = 0;
    setAwaitingQuantity();
    display_.showQuantity("Checking inventory", String("Part ") + scannedCode_,
                          quantity_.displayText(), quantity_.cursor(), true,
                          QuantityLookupState::Pending);
    lastSyncAttempt_ = 0;
    deviceLog.print("Decoded default quantity: ");
    deviceLog.println(quantity_.displayText().c_str());
    deviceLog.println("Enter quantity, then press A to add. C cancels.");
    sendDebugMessage("data matrix scan pending", "scan");
    return;
  }

  TmeBagLabel tmeLabel;
  if (parseTmeBagLabel(trimmed, tmeLabel)) {
    const auto tmeCode = tmeBagCodeForSync(tmeLabel, kMaximumBagEventCodeBytes);
    if (tmeCode.length() == 0) {
      showScanRefusal("Label too long to send. Add the part by hand", "Rejected TME label scan that is too long.");
      return;
    }
    resetPending();
    scannedCode_ = tmeLabel.manufacturerPartNumber;
    scannedEventCode_ = tmeCode;
    quantity_.setDefaultValue(tmeLabel.quantity);
    pendingScanType_ = PendingScanType::ComponentDataMatrix;
    // The desktop matches lookups on SKU, which a TME label fills with its TME symbol. The
    // manufacturer part number is not searched, so it would never find the part.
    activeLookupCode_ = tmeLabel.symbol;
    activeLookupId_ = clientConfig_.deviceId + String("-lookup-") + String(lookupSequence_++);
    lookupMissCount_ = 0;
    setAwaitingQuantity();
    display_.showQuantity("Checking inventory", String("Part ") + scannedCode_,
                          quantity_.displayText(), quantity_.cursor(), true,
                          QuantityLookupState::Pending);
    lastSyncAttempt_ = 0;
    deviceLog.print("Decoded default quantity: ");
    deviceLog.println(quantity_.displayText().c_str());
    deviceLog.println("Enter quantity, then press A to add. C cancels.");
    sendDebugMessage("tme label scan pending", "scan");
    return;
  }

  if (isQrScan(raw) || isQuantityCode(trimmed)) {
    if (!isStandardMachineCode(trimmed)) {
      deviceLog.println("Rejected non-standard inventory ID scan.");
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
    deviceLog.println("Enter quantity, then press A to add or B to subtract. C cancels.");
    sendDebugMessage("inventory scan pending", "scan");
    return;
  }

  // A nearby code (such as on a TME label) can be read after the scan. While the scan awaits its
  // quantity, the extra frame is logged and does not replace its screen.
  if (state_ == State::AwaitQuantity && scannedCode_.length() > 0) {
    deviceLog.println("Ignored unrecognized frame while a scan is pending.");
    return;
  }
  showScanRefusal("Scan not recognised. Add the part by hand", "Rejected unrecognized scanner data.");
}

SyncResult InventatoryScanApp::localFailure(const char* id, const char* code, const char* message) {
  SyncResult result;
  result.resultId = id;
  result.eventId = id;
  result.status = "failed";
  result.code = code;
  result.message = message;
  return result;
}

void InventatoryScanApp::openMenu() {
  state_ = State::Menu;
  display_.showMenu(outboxBlockedHead_.blocked(), outbox_.unreadable());
}

void InventatoryScanApp::returnToReady() {
  state_ = State::Idle;
  display_.showReady();
}

void InventatoryScanApp::handleKey(const InventatoryKeyEvent& event) {
  noteUserActivity();
  // Low-battery warning: only the power-off hold is honoured.
  if (lowBatteryWarning_ && event.type != InventatoryKeyEventType::PowerOff) return;
  // A key press dismisses a countdown warning. Short presses, D holds and # holds are used up;
  // '*' and the update gesture still act.
  if (energyWarningShown()) {
    const bool used = isShortKeyPress(event.type) || event.type == InventatoryKeyEventType::PowerToggle ||
                      event.type == InventatoryKeyEventType::ProvisioningReset;
    dismissEnergyWarning();
    if (used) return;
  }
  // A short press wakes a device asleep on its energy saving timer, and does nothing else.
  // Update requests run in standby, so their cancel and upload keep their own handling.
  if (energyPhase_ == EnergyPhase::Asleep && powerMode_ == PowerMode::Standby && !otaRequested_ && !otaActive_ &&
      isShortKeyPress(event.type)) {
    deviceLog.println("Woke from energy saving sleep.");
    exitStandby();
    return;
  }
  const bool updating = otaRequested_ || otaActive_;
  switch (event.type) {
    case InventatoryKeyEventType::ProvisioningReset:
      // Holding # only opens the confirmation screen; the wipe itself needs A.
      if (setupMode_ || state_ != State::Idle || powerMode_ == PowerMode::Standby || updating) return;
      state_ = State::ConfirmReprovision;
      reprovisionFromHold_ = true;
      display_.showResetConfirm();
      return;
    // The power-off hold also works in setup (AGENTS.md section 9): deep sleep ends BLE pairing.
    // One second opens the countdown; release, D or # cancels it; four seconds powers off.
    case InventatoryKeyEventType::PowerOffWarning:
      if (state_ == State::Idle && !updating) startPowerOffWarning();
      return;
    case InventatoryKeyEventType::PowerOffCancel:
      cancelPowerOffWarning();
      return;
    case InventatoryKeyEventType::PowerOff:
      if (state_ == State::Idle && !updating) enterPowerOff();
      return;
    default:
      break;
  }
  if (setupMode_) {
    // A key press is the physical confirmation that re-opens BLE pairing after a lockout.
    if (provisioning_.locked() && event.type == InventatoryKeyEventType::Add) provisioning_.rearm();
    return;
  }
  if (otaRequested_ && !otaUploadStarted_ && event.type == InventatoryKeyEventType::Cancel) {
    abandonOtaWait("OTA wait cancelled.", false);
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
      deviceLog.println("Waiting for Inventatory to confirm the submitted change.");
    }
    return;
  }

  switch (event.type) {
    case InventatoryKeyEventType::Digit:
      handleDigit(event.value);
      break;
    case InventatoryKeyEventType::CycleCursor:
      if (state_ == State::QuickLabels && quickLabelPresetCount_ > kQuickLabelsPerPage) {
        const uint8_t pages =
            static_cast<uint8_t>((quickLabelPresetCount_ + kQuickLabelsPerPage - 1U) / kQuickLabelsPerPage);
        quickLabelPage_ = static_cast<uint8_t>((quickLabelPage_ + 1U) % pages);
        display_.showQuickLabels(quickLabelPresets_, quickLabelPresetCount_, quickLabelPage_,
                                 quickLabelSnapshotReceived_, pcSupportsQuickLabels_);
      } else if (state_ == State::EnergySaving) {
        cycleEnergyPage();
      } else if (state_ == State::AwaitQuantity) {
        quantity_.cycleCursor();
        display_.updateQuantity(quantity_.displayText(), quantity_.cursor());
      }
      break;
    case InventatoryKeyEventType::DeleteDigit:
      if (state_ == State::AwaitQuantity) {
        quantity_.deleteDigit();
        display_.updateQuantity(quantity_.displayText(), quantity_.cursor());
        deviceLog.print("Quantity: ");
        deviceLog.println(quantity_.displayText().c_str());
      } else if (state_ == State::AwaitBatteryCapacity) {
        deleteBatteryCapacityDigit();
      } else if ((state_ == State::Idle || state_ == State::Menu) && display_.hasProblem()) {
        // Short #: explain the problem on show, from the main screen or the menu.
        faultDetailReturn_ = state_;
        state_ = State::FaultDetail;
        display_.showFaultDetail();
      }
      break;
    case InventatoryKeyEventType::Cancel:
      handleCancel();
      break;
    case InventatoryKeyEventType::Add:
    case InventatoryKeyEventType::Subtract:
      handleAddSubtract(event.type == InventatoryKeyEventType::Add);
      break;
    default:
      break;
  }
}

void InventatoryScanApp::handleDigit(char digit) {
  switch (state_) {
    case State::Menu:
      if (digit == '1') {
        openQuickLabels();
      } else if (digit == '2') {
        openQuickSettings();
      } else if (digit == '3' && outbox_.unreadable()) {
        state_ = State::ConfirmRepair;
        display_.showRepairConfirm(outbox_.incompatible());
      } else if (digit == '3' && outboxBlockedHead_.blocked()) {
        state_ = State::ConfirmDiscard;
        display_.showDiscardConfirm();
      }
      return;
    case State::QuickLabels: {
      const uint8_t selected = static_cast<uint8_t>(digit - '0');
      if (selected >= 1 && selected <= kQuickLabelsPerPage) {
        requestQuickLabelPrint(static_cast<uint8_t>(quickLabelPage_ * kQuickLabelsPerPage + selected));
      }
      return;
    }
    case State::QuickSettings:
      if (digit == '1') {
        openDisplaySettings();
      } else if (digit == '2') {
        energyPage_ = 0;
        openEnergySaving();
      } else if (digit == '3') {
        state_ = State::ConfirmReprovision;
        reprovisionFromHold_ = false;
        display_.showResetConfirm();
      } else if (digit == '4') {
        state_ = State::Idle;
        requestOtaUpdate();
      }
      return;
    case State::DisplaySettings:
      if (digit == '1') {
        state_ = State::AdjustContrast;
        display_.showContrast(lcdContrast_);
      } else if (digit == '2') {
        openBacklightSettings();
      }
      return;
    case State::EnergySaving:
      // Page 1/2 holds the switch, the delays and dock mode; page 2/2 holds the battery capacity.
      if (energyPage_ == 0) {
        if (digit == '1') {
          toggleEnergySaving();
        } else if (digit == '2') {
          openSleepAfterSettings();
        } else if (digit == '3') {
          openPowerOffSettings();
        } else if (digit == '4') {
          toggleDockMode();
        }
      } else if (digit == '1') {
        openBatteryCapacityEntry();
      }
      return;
    case State::AwaitBatteryCapacity:
      appendBatteryCapacityDigit(digit);
      return;
    case State::Idle:
      deviceLog.println("Digit ignored, scan a code first.");
      return;
    case State::AwaitQuantity:
      quantity_.appendDigit(digit);
      display_.updateQuantity(quantity_.displayText(), quantity_.cursor());
      deviceLog.print("Quantity: ");
      deviceLog.println(quantity_.displayText().c_str());
      return;
    default:
      // Other screens take no digits and must not fall into quantity entry.
      return;
  }
}

// C goes back one level, or cancels what is open.
void InventatoryScanApp::handleCancel() {
  switch (state_) {
    case State::Idle:
      if (display_.errorVisible()) {
        display_.dismissError();
      } else {
        deviceLog.println("Opening device menu.");
        openMenu();
      }
      return;
    case State::ConfirmDiscard:
    case State::ConfirmRepair:
      openMenu();
      return;
    case State::FaultDetail:
      // Return to the screen the fault details were opened from.
      if (faultDetailReturn_ == State::Menu) {
        openMenu();
      } else {
        returnToReady();
      }
      return;
    case State::Menu:
    case State::QuickLabels:
    case State::QuickSettings:
    case State::PrintingLabel:
      pendingQuickLabelPrintId_.clear();
      pendingQuickLabelPresetIndex_ = 0;
      returnToReady();
      return;
    case State::AdjustContrast:
      persistContrast();
      openDisplaySettings();
      return;
    case State::AdjustBacklight:
      // Each change is already saved; C only leaves the picker.
      openDisplaySettings();
      return;
    case State::AdjustSleepAfter:
    case State::AdjustPowerOff:
    case State::AwaitBatteryCapacity:
      openEnergySaving();
      return;
    case State::EnergySaving:
    case State::DisplaySettings:
      openQuickSettings();
      return;
    case State::ConfirmReprovision:
      if (reprovisionFromHold_) {
        reprovisionFromHold_ = false;
        returnToReady();
      } else {
        openQuickSettings();
      }
      return;
    default:
      resetPending();
      display_.finishInteraction();
      deviceLog.println("Cancelled pending scan.");
      return;
  }
}

// A and B: step a setting, confirm a dialog (A only), or submit the quantity.
void InventatoryScanApp::handleAddSubtract(bool add) {
  switch (state_) {
    case State::AdjustContrast:
      if (add && lcdContrast_ > 0) --lcdContrast_;
      if (!add && lcdContrast_ < 63) ++lcdContrast_;
      display_.setContrast(lcdContrast_);
      persistContrast();
      display_.showContrast(lcdContrast_);
      return;
    // A moves towards the first option (Off, or the shortest delay), B towards the last.
    case State::AdjustBacklight: stepBacklightMode(add ? -1 : 1); return;
    case State::AdjustSleepAfter: stepSleepDelay(add ? -1 : 1); return;
    case State::AdjustPowerOff: stepPowerOffDelay(add ? -1 : 1); return;
    case State::AwaitBatteryCapacity:
      if (add) confirmBatteryCapacity();
      return;
    case State::ConfirmReprovision:
      if (add) confirmReprovision();
      return;
    case State::ConfirmRepair:
      if (add) confirmRepair();
      return;
    case State::ConfirmDiscard:
      if (add) confirmDiscard();
      return;
    case State::AwaitQuantity:
      break;
    default:
      deviceLog.println("No scanned code to submit.");
      return;
  }
  if (scannedCode_.length() == 0) {
    deviceLog.println("No scanned code to submit.");
    return;
  }
  if (pendingScanType_ == PendingScanType::ComponentDataMatrix && !add) {
    deviceLog.println("Component scans use A to confirm the quantity.");
    return;
  }
  submitCurrent(add ? 'A' : 'B');
}

void InventatoryScanApp::confirmReprovision() {
  const bool paused = pauseNetworkForSetupTransition();
  if (paused && setupStore_.clear()) {
    deviceLog.println("Re-pair requested from quick settings; restarting BLE setup.");
    ESP.restart();
  }
  if (paused) resumeNetworkAfterSetupTransition();
  display_.showLocalFailure(localFailure("local-reset", kPairingClearFailedCode, "Could not clear pairing"));
  state_ = State::Idle;
}

void InventatoryScanApp::confirmRepair() {
  std::size_t salvaged = 0;
  std::size_t dropped = 0;
  const auto result = outbox_.repair(salvaged, dropped);
  if (result == OutboxStore::RepairResult::Repaired) {
    deviceLog.println("Scan queue repaired.");
    display_.setQueueUnreadable(false);
    returnToReady();
  } else if (result == OutboxStore::RepairResult::StorageFailed) {
    display_.showLocalFailure(localFailure("local-queue", kRepairFailedCode, "Could not repair queue"));
    state_ = State::Idle;
  } else {
    returnToReady();
  }
}

void InventatoryScanApp::confirmDiscard() {
  if (outbox_.pop()) {
    outboxBlockedHead_.recordProgress();
    display_.setSyncBlocked(false);
    deviceLog.println("Discarded the oldest queued scan at the operator request.");
    returnToReady();
  } else {
    display_.showLocalFailure(localFailure("local-queue", kDiscardFailedCode, "Could not discard scan"));
    state_ = State::Idle;
  }
}

void InventatoryScanApp::submitCurrent(char action) {
  if (pendingScanType_ == PendingScanType::ComponentDataMatrix) {
    if (action != 'A') {
      deviceLog.println("Component scans require A to confirm.");
      return;
    }

    const auto quantity = quantity_.consume(true);
    String eventId;
    const auto queued = queueEvent(DeviceEventKind::InventoryReceive,
                                   scannedEventCode_.length() > 0 ? scannedEventCode_ : scannedCode_, quantity, &eventId,
                                   scannedManufacturer_, scannedManufacturerPartNumber_,
                                   scannedPartName_);
    resetPending();
    if (!queued) return;  // The failure screen is already showing.
    deviceLog.print("Queued decoded-component quantity ");
    deviceLog.println(String(quantity).c_str());
    waitForInventoryResult(eventId);
    return;
  }

  const auto delta = quantity_.consume(action == 'A');
  String eventId;
  const auto queued = queueEvent(DeviceEventKind::InventoryAdjust, scannedCode_, delta, &eventId);
  resetPending();
  if (!queued) return;  // The failure screen is already showing.
  waitForInventoryResult(eventId);
}

// Nothing is queued or added, and the scanner stays ready. The screen shows the short reason;
// the serial log shows the detail.
void InventatoryScanApp::showScanRefusal(const char* screenMessage, const char* logMessage) {
  display_.showLocalFailure(localFailure("scan-refused", kScanRejectedCode, screenMessage));
  deviceLog.println(logMessage);
}

bool InventatoryScanApp::queueEvent(DeviceEventKind kind, const String& code, int value, String* eventId,
                                    const String& manufacturer, const String& manufacturerPartNumber,
                                    const String& encodedPartName) {
  DeviceEventRecord event;
  const auto queueResult =
      outbox_.enqueue(kind, code, value, &event, manufacturer, manufacturerPartNumber, encodedPartName);
  if (queueResult != OutboxStore::EnqueueResult::Queued) {
    const bool full = queueResult == OutboxStore::EnqueueResult::QueueFull;
    const bool storage = queueResult == OutboxStore::EnqueueResult::StorageFailed;
    const auto result = localFailure("local-queue", full ? kQueueFullCode : storage ? kScanNotSavedCode : kScanRejectedCode,
                                     full      ? "Sync PC before scanning"
                                     : storage ? "Scan storage write failed"
                                               : "Scan data cannot be saved");
    display_.showLocalFailure(result);
    deviceLog.println(result.message.c_str());
    return false;
  }
  if (eventId != nullptr) *eventId = event.eventId;
  deviceLog.println("Queued protocol v1 inventory event.");
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
  scannedEventCode_.clear();
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