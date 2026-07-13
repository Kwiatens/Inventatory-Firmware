#include "display/display_controller.h"

namespace hims_scan {

void DisplayController::begin() {
  renderer_.begin();
  showBoot("DISPLAY OK");
  tick();
}

void DisplayController::markDirty() { dirty_ = true; }

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
  renderer_.setPowerSave(standby);
  if (!standby) showReady();
}

void DisplayController::showOta(const String& text, int percent) {
  renderer_.setPowerSave(false);
  model_.mode = DisplayUiMode::Ota;
  model_.title = text;
  model_.otaPercent = percent;
  model_.footer = "SCANNING PAUSED";
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
  if (!dirty_) return;
  renderer_.render(model_, pulseFrame);
  dirty_ = false;
}

}  // namespace hims_scan
