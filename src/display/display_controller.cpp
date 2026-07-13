#include "display/display_controller.h"

#include "config/hardware.h"

namespace hims_scan {

void DisplayController::begin() {
  renderer_.begin();
  showBoot("DISPLAY OK");
  tick();
}

void DisplayController::markDirty() { dirty_ = true; }

void DisplayController::clearMenuLines() {
  model_.menuLineCount = 0;
  for (auto& line : model_.menuLines) line.clear();
}

void DisplayController::setConnectivity(DisplayConnectivityState state, bool wifiOnline, bool pcOnline,
                                        std::size_t queueDepth) {
  if (model_.connectivity == state && model_.wifiOnline == wifiOnline && model_.pcOnline == pcOnline &&
      model_.queueDepth == queueDepth) return;
  model_.connectivity = state;
  model_.wifiOnline = wifiOnline;
  model_.pcOnline = pcOnline;
  model_.queueDepth = queueDepth;
  if (state != DisplayConnectivityState::Online) model_.headerAlternate = false;
  if (model_.mode == DisplayUiMode::Ready) {
    model_.line1 = "READY";
    model_.line2 = queueDepth > 0 ? String("PENDING ") + queueDepth : String();
  }
  markDirty();
}

void DisplayController::showSplash() {
  model_.mode = DisplayUiMode::Splash;
  model_.title = "INVENTATORY";
  model_.line1 = "SCAN R1";
  model_.line2.clear();
  model_.footer.clear();
  markDirty();
}

void DisplayController::showBoot(const String& stage) {
  model_.mode = DisplayUiMode::Boot;
  model_.title = "STARTING";
  model_.line1 = stage;
  model_.line2.clear();
  model_.footer = "PLEASE WAIT";
  markDirty();
}

void DisplayController::showReady() {
  model_.mode = DisplayUiMode::Ready;
  model_.title = "SCAN ITEM";
  model_.line1 = "READY";
  model_.line2 = model_.queueDepth > 0 ? String("PENDING ") + model_.queueDepth : String();
  model_.footer = "HOLD D: STANDBY";
  resultUntilMs_ = 0;
  clearMenuLines();
  markDirty();
}

void DisplayController::showQuantity(const String& itemText, const String& quantityText,
                                     std::size_t cursor, bool supplier) {
  model_.mode = DisplayUiMode::Quantity;
  model_.supplierQuantity = supplier;
  model_.title = supplier ? "CONFIRM RECEIPT" : "CHANGE QTY FOR";
  model_.line1 = itemText;
  model_.quantityText = quantityText;
  model_.quantityCursor = cursor;
  model_.footer = supplier ? "A OK  C CANCEL  D CUR" : "A+ B- C CANCEL D CUR";
  markDirty();
}

void DisplayController::updateQuantity(const String& quantityText, std::size_t cursor) {
  model_.quantityText = quantityText;
  model_.quantityCursor = cursor;
  markDirty();
}

void DisplayController::updateQuantityItem(const String& itemText) {
  if (model_.mode != DisplayUiMode::Quantity || model_.line1 == itemText) return;
  model_.line1 = itemText;
  markDirty();
}

void DisplayController::pulseScan() {
  pulseUntilMs_ = millis() + 350U;
  markDirty();
}

void DisplayController::showQueued() {
  model_.mode = DisplayUiMode::Queued;
  model_.title = model_.pcOnline ? "SENT TO PC" : "SAVED OFFLINE";
  model_.line1 = String("QUEUE ") + model_.queueDepth;
  model_.line2 = "SCAN NEXT ITEM";
  model_.footer = "PROCESSING";
  resultUntilMs_ = millis() + 1200U;
  markDirty();
}

void DisplayController::applyResult(const SyncResult& result) {
  const bool failed = result.status == "failed";
  const bool warning = result.status == "completed_with_warning";
  model_.mode = failed ? DisplayUiMode::Error : (warning ? DisplayUiMode::Warning : DisplayUiMode::Success);
  model_.title = failed ? (result.code.length() > 0 ? result.code : String("ERROR"))
                        : (result.existing ? String("EXISTING ITEM") : String("ITEM RECEIVED"));
  model_.line1 = failed ? result.message : result.itemName;
  model_.line2 = failed ? String() : String(result.appliedDelta >= 0 ? "+" : "") + result.appliedDelta +
                                      " QTY " + result.quantity + " " + result.location;
  model_.footer = failed ? "C DISMISS" : (warning ? "WARNING" : "DONE");
  resultUntilMs_ = failed ? 0 : millis() + 4000U;
  markDirty();
}

bool DisplayController::showResult(const SyncResult& result) {
  if (model_.mode == DisplayUiMode::Quantity) {
    if (deferredResult_) return false;
    pendingResult_ = result;
    deferredResult_ = true;
    return true;
  }
  if (model_.mode == DisplayUiMode::Success || model_.mode == DisplayUiMode::Warning ||
      model_.mode == DisplayUiMode::Error) {
    return false;
  }
  applyResult(result);
  return true;
}

bool DisplayController::finishInteraction() {
  if (deferredResult_) {
    deferredResult_ = false;
    applyResult(pendingResult_);
    return true;
  } else {
    showReady();
    return false;
  }
}

void DisplayController::dismissError() {
  if (model_.mode == DisplayUiMode::Error) showReady();
}

void DisplayController::setStandby(bool standby) {
  standby_ = standby;
  renderer_.setPowerSave(standby);
  if (!standby) showReady();
}

void DisplayController::setContrast(uint8_t contrast) {
  renderer_.setContrast(contrast);
  markDirty();
}

void DisplayController::showOta(const String& text, int percent) {
  standby_ = false;
  renderer_.setPowerSave(false);
  model_.mode = DisplayUiMode::Ota;
  model_.title = text;
  model_.otaPercent = percent;
  model_.footer = "SCANNING PAUSED";
  markDirty();
}

void DisplayController::showSetup(uint32_t pairingCode, const String& status) {
  standby_ = false;
  renderer_.setPowerSave(false);
  model_.mode = DisplayUiMode::Setup;
  model_.title = "BLE SETUP";
  model_.line1 = status;
  model_.line2 = String("CODE ") + String(pairingCode);
  model_.setupCode = pairingCode;
  model_.footer = "USE INVENTATORY PC";
  markDirty();
}

void DisplayController::showMenu() {
  model_.mode = DisplayUiMode::Menu;
  model_.title = "MENU  C/BACK";
  clearMenuLines();
  model_.menuLines[model_.menuLineCount++] = "1 QUICK LABELS";
  model_.menuLines[model_.menuLineCount++] = "2 QUICK SETTINGS";
  model_.footer.clear();
  markDirty();
}

void DisplayController::showQuickLabels(const String* labels, std::size_t count, std::size_t page,
                                        bool snapshotReceived, bool pcSupportsQuickLabels) {
  model_.mode = DisplayUiMode::QuickLabels;
  model_.title = count > 4U ? "LABELS D/NEXT C/BACK" : "LABELS C/BACK";
  clearMenuLines();
  if (!snapshotReceived) {
    model_.menuLines[model_.menuLineCount++] = "SYNCING PC PRESETS";
  } else if (!pcSupportsQuickLabels) {
    model_.menuLines[model_.menuLineCount++] = "UPDATE PC SOFTWARE";
    model_.menuLines[model_.menuLineCount++] = "LABELS NOT SUPPORTED";
  } else {
    const auto first = page * 4U;
    for (std::size_t index = 0; index < 4U && first + index < count; ++index) {
      model_.menuLines[model_.menuLineCount++] = String(index + 1U) + " " + labels[first + index];
    }
    if (model_.menuLineCount == 0) {
      model_.menuLines[model_.menuLineCount++] = "NO PRESETS ON PC";
      model_.menuLines[model_.menuLineCount++] = "ADD IN QUICK LABELS";
    }
  }
  model_.footer.clear();
  markDirty();
}

void DisplayController::showQuickSettings(uint8_t contrast) {
  model_.mode = DisplayUiMode::QuickSettings;
  model_.title = "SETTINGS C/BACK";
  clearMenuLines();
  model_.menuLines[model_.menuLineCount++] = "1  LCD CONTRAST  " + String(contrast);
  model_.menuLines[model_.menuLineCount++] = "2  STANDBY";
  model_.menuLines[model_.menuLineCount++] = "3  RE-PAIR DEVICE";
  model_.menuLines[model_.menuLineCount++] = "4  OTA UPDATE";
  model_.footer.clear();
  markDirty();
}

void DisplayController::showContrast(uint8_t contrast) {
  model_.mode = DisplayUiMode::Contrast;
  model_.title = "LCD CONTRAST";
  model_.line1 = String(contrast);
  model_.line2 = "A- B+";
  model_.footer = "C SAVE AND BACK";
  clearMenuLines();
  markDirty();
}

void DisplayController::showResetConfirm() {
  model_.mode = DisplayUiMode::ConfirmReset;
  model_.title = "RE-PAIR R1?";
  model_.line1 = "WIFI WILL BE ERASED";
  model_.line2 = "OUTBOX IS KEPT";
  model_.footer = "A CONFIRM C CANCEL";
  clearMenuLines();
  markDirty();
}

void DisplayController::showPrinting(const String& label) {
  model_.mode = DisplayUiMode::Printing;
  model_.title = "PRINTING LABEL";
  model_.line1 = label;
  model_.line2 = "WAITING FOR PC";
  model_.footer = "C CANCEL";
  clearMenuLines();
  markDirty();
}

void DisplayController::showQuickLabelPrintResult(bool success, const String& message) {
  model_.mode = success ? DisplayUiMode::Success : DisplayUiMode::Error;
  model_.title = success ? "LABEL SENT" : "PRINT FAILED";
  model_.line1 = message;
  model_.line2.clear();
  model_.footer = success ? "DONE" : "C DISMISS";
  resultUntilMs_ = success ? millis() + 3000U : 0;
  clearMenuLines();
  markDirty();
}

void DisplayController::tick() {
  const auto now = millis();
  if (model_.connectivity == DisplayConnectivityState::Online &&
      now - lastHeaderAlternateMs_ >= 2500U) {
    lastHeaderAlternateMs_ = now;
    model_.headerAlternate = !model_.headerAlternate;
    dirty_ = true;
  }
  const bool pulseFrame = pulseUntilMs_ > now && ((now / 100U) % 2U == 0U);
  if (pulseFrame != lastPulseFrame_) {
    lastPulseFrame_ = pulseFrame;
    dirty_ = true;
  }
  if (resultUntilMs_ != 0 && static_cast<long>(now - resultUntilMs_) >= 0) {
    resultUntilMs_ = 0;
    showReady();
  }
  if (!dirty_) {
    if (!standby_ && now - lastStartLineRecoveryMs_ >= hardware::kLcdStartLineRecoveryIntervalMs) {
      renderer_.normalizeStartLine();
      lastStartLineRecoveryMs_ = now;
    }
    return;
  }
  renderer_.render(model_, pulseFrame);
  dirty_ = false;
  lastStartLineRecoveryMs_ = now;
}

}  // namespace hims_scan
