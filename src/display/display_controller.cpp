#include "display/display_controller.h"

namespace hims_scan {

void DisplayController::begin() {
  renderer_.begin();
  showBoot("DISPLAY OK");
  tick();
}

void DisplayController::markDirty() { dirty_ = true; }

void DisplayController::setConnectivity(bool wifiOnline, bool pcOnline, std::size_t queueDepth) {
  if (model_.wifiOnline == wifiOnline && model_.pcOnline == pcOnline && model_.queueDepth == queueDepth) return;
  model_.wifiOnline = wifiOnline;
  model_.pcOnline = pcOnline;
  model_.queueDepth = queueDepth;
  if (model_.mode == DisplayUiMode::Ready) {
    model_.line1 = pcOnline ? "PC ONLINE" : "OFFLINE - WILL QUEUE";
    model_.line2 = queueDepth > 0 ? String("PENDING ") + queueDepth : String("READY");
  }
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
  model_.line1 = model_.pcOnline ? "PC ONLINE" : "OFFLINE - WILL QUEUE";
  model_.line2 = model_.queueDepth > 0 ? String("PENDING ") + model_.queueDepth : String("READY");
  model_.footer = "HOLD D: STANDBY";
  resultUntilMs_ = 0;
  markDirty();
}

void DisplayController::showQuantity(const String& code, int quantity, bool supplier) {
  model_.mode = DisplayUiMode::Quantity;
  model_.supplierQuantity = supplier;
  model_.title = supplier ? String("CONFIRM ") + code : String("QTY FOR ") + code;
  model_.quantity = quantity;
  model_.footer = supplier ? "A OK   C CANCEL" : "A +  B -  C CANCEL";
  markDirty();
}

void DisplayController::updateQuantity(int quantity) {
  model_.quantity = quantity;
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
