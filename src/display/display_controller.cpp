// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#include "display/display_controller.h"

#include <algorithm>

#include "InventatoryScanUiPolicy.h"
#include "config/hardware.h"
#include "config/version.h"
#include "display/display_style.h"

namespace inventatory_scan {

namespace {

// Rows the list body can show; anything beyond this is never drawn.
constexpr std::size_t kListRowLimit = style::kListMaxRows;
// Quick labels are paged four to a screen.
constexpr std::size_t kQuickLabelsPerPage = 4U;

static_assert(kListRowLimit <= sizeof(DisplayModel::listRows) / sizeof(DisplayModel::listRows[0]),
              "list row limit exceeds DisplayModel::listRows capacity");
static_assert(kQuickLabelsPerPage <= kListRowLimit, "a quick-label page must fit the list");

// Appends one list row. Drops the row (returns false) instead of writing past the
// end of the fixed listRows array or past the rows the renderer can show.
bool appendListRow(DisplayModel& model, const String& row) {
  if (model.listRowCount >= kListRowLimit) return false;
  model.listRows[model.listRowCount++] = row;
  return true;
}

}  // namespace

void DisplayController::begin() {
  renderer_.begin();
  dirty_ = true;
}

void DisplayController::markDirty() { dirty_ = true; }

void DisplayController::clearContent() {
  // Every screen change cancels a pending result dwell; the result setters
  // re-arm it after calling this.
  resultUntilMs_ = 0;
  model_.animateEllipsis = false;
  model_.brandDot = DisplayBrandDot::Full;
  model_.introStep = kBrandIntroSteps;
  model_.header.clear();
  model_.primary.clear();
  model_.secondary.clear();
  model_.tertiary.clear();
  model_.actions.clear();
  model_.pageText.clear();
  model_.quantityPrompt.clear();
  model_.listRowCount = 0;
  for (auto& row : model_.listRows) row.clear();
}

void DisplayController::refreshReadyContent() {
  model_.header = "Inventatory";
  model_.primary = "Ready to scan";
  if (queueUnreadable_) {
    model_.secondary = "Queue unreadable: C, 3";
  } else if (syncBlocked_) {
    model_.secondary = "Sync blocked: C, 3";
  } else if (displayQueueDepth(model_.queueDepth)) {
    model_.secondary = String(model_.queueDepth) + " scans safely queued";
  } else if (model_.connectivity == DisplayConnectivityState::Syncing) {
    model_.secondary = "Sending saved work";
  } else if (model_.connectivity == DisplayConnectivityState::Online) {
    model_.secondary = "Point at a label";
  } else {
    model_.secondary = "Scans save on device";
  }
  model_.rail = model_.connectivity == DisplayConnectivityState::Syncing
                    ? DisplayRailStyle::Activity
                    : DisplayRailStyle::Static;
}

void DisplayController::setSyncBlocked(bool blocked) {
  if (syncBlocked_ == blocked) return;
  syncBlocked_ = blocked;
  if (model_.mode == DisplayUiMode::Ready) refreshReadyContent();
  markDirty();
}

void DisplayController::setQueueUnreadable(bool unreadable) {
  if (queueUnreadable_ == unreadable) return;
  queueUnreadable_ = unreadable;
  if (model_.mode == DisplayUiMode::Ready) refreshReadyContent();
  markDirty();
}

void DisplayController::setConnectivity(DisplayConnectivityState state, bool wifiOnline, bool pcOnline,
                                        std::size_t queueDepth) {
  (void)wifiOnline;
  (void)pcOnline;
  if (model_.connectivity == state && model_.queueDepth == queueDepth) return;
  model_.connectivity = state;
  model_.queueDepth = queueDepth;
  if (model_.mode == DisplayUiMode::Ready) refreshReadyContent();
  markDirty();
}

void DisplayController::showSplash() {
  clearContent();
  model_.mode = DisplayUiMode::Splash;
  model_.primary = "Inventatory";
  model_.secondary = "Scanner R1";
  model_.tertiary = String("v") + kFirmwareVersion;
  model_.rail = DisplayRailStyle::None;
  model_.showPcStatus = false;
  markDirty();
}

void DisplayController::playBootIntro() {
  showSplash();
  const unsigned long start = millis();
  model_.introStep = -1;
  for (;;) {
    const unsigned long now = millis();
    const int step = brandIntroStep(now - start);
    if (step != model_.introStep) {
      model_.introStep = step;
      lastActivity_ = computeActivity(now);
      renderer_.render(model_, lastActivity_);
      lastStartLineRecoveryMs_ = now;
    }
    if (step >= kBrandIntroSteps) break;
    delay(10);
  }
  dirty_ = false;
}

// Boot keeps the splash lockup on screen until the scanner is ready; a failed
// stage replaces the product name and breaks the dot.
void DisplayController::showBoot(const String& stage) {
  showSplash();
  model_.mode = DisplayUiMode::Boot;
  if (stage.length() == 0) return;
  model_.primary = stage;
  model_.secondary = "See USB serial";
  model_.brandDot = DisplayBrandDot::Fault;
}

void DisplayController::showReady() {
  clearContent();
  model_.mode = DisplayUiMode::Ready;
  model_.bodyKind = DisplayBodyKind::Task;
  model_.showPcStatus = true;
  model_.actions = "C Menu";
  resultUntilMs_ = 0;
  refreshReadyContent();
  markDirty();
}

void DisplayController::showQuantity(const String& itemText, const String& itemCode,
                                     const String& quantityText, std::size_t cursor, bool receiveMode,
                                     QuantityLookupState lookupState) {
  clearContent();
  model_.mode = DisplayUiMode::Quantity;
  model_.bodyKind = DisplayBodyKind::Quantity;
  model_.showPcStatus = true;
  model_.receiveQuantity = receiveMode;
  model_.quantityPrompt = receiveMode ? "Add quantity:" : "Change quantity by:";
  model_.primary = itemText;
  model_.secondary = itemCode;
  model_.quantityText = quantityText;
  model_.quantityCursor = cursor;
  model_.actions = receiveMode ? "A + # Del C Cancel" : "A + B - # Del C Cancel";
  updateQuantityLookup(itemText, lookupState);
  markDirty();
}

void DisplayController::updateQuantity(const String& quantityText, std::size_t cursor) {
  model_.quantityText = quantityText;
  model_.quantityCursor = cursor;
  markDirty();
}

void DisplayController::updateQuantityLookup(const String& itemText,
                                             QuantityLookupState lookupState) {
  if (model_.mode != DisplayUiMode::Quantity) return;
  const String previousPrimary = model_.primary;
  const String previousHeader = model_.header;
  const DisplayRailStyle previousRail = model_.rail;
  model_.primary = itemText;
  switch (lookupState) {
    case QuantityLookupState::Pending:
      model_.header = "Scan details";
      model_.rail = DisplayRailStyle::Activity;
      break;
    case QuantityLookupState::Found:
      model_.header = "Part found";
      model_.rail = DisplayRailStyle::Static;
      break;
    case QuantityLookupState::NotFound:
      model_.header = model_.receiveQuantity ? "New part" : "Part not found";
      model_.rail = DisplayRailStyle::Static;
      break;
    case QuantityLookupState::Unavailable:
      model_.header = "Part details";
      model_.rail = DisplayRailStyle::Static;
      break;
  }
  if (model_.primary != previousPrimary || model_.header != previousHeader ||
      model_.rail != previousRail) {
    markDirty();
  }
}

void DisplayController::showSending() {
  clearContent();
  model_.mode = DisplayUiMode::Sending;
  model_.bodyKind = DisplayBodyKind::Task;
  model_.rail = DisplayRailStyle::Activity;
  model_.showPcStatus = true;
  model_.animateEllipsis = true;
  model_.header = "Sending";
  model_.primary = "Sending to Inventatory";
  model_.secondary = "Waiting for response";
  model_.actions = "Waiting for PC";
  resultUntilMs_ = 0;
  markDirty();
}

void DisplayController::applyResult(const SyncResult& result) {
  clearContent();
  const bool completed = result.status == "completed";
  const bool warning = result.status == "completed_with_warning";
  const bool failed = result.status == "failed" || (!completed && !warning);
  model_.mode = failed ? DisplayUiMode::Error : (warning ? DisplayUiMode::Warning : DisplayUiMode::Success);
  model_.bodyKind = DisplayBodyKind::Task;
  model_.showPcStatus = true;
  if (failed) {
    model_.rail = DisplayRailStyle::Static;
    model_.header = "Needs attention";
    model_.primary = result.message.length() > 0 ? result.message : String("Could not update inventory");
    model_.secondary = result.code.length() > 0 ? result.code : String("Try again");
    model_.actions = "C Dismiss";
    resultUntilMs_ = 0;
  } else if (warning) {
    model_.rail = DisplayRailStyle::Static;
    model_.header = "Check result";
    model_.primary = result.itemName.length() > 0 ? result.itemName : String("Inventory updated");
    model_.secondary = result.message.length() > 0 ? result.message : String("Updated with warning");
    model_.tertiary = result.purposeLabel.length() > 0 ? result.purposeLabel : String("Review on your PC");
    model_.actions = "C Dismiss";
    resultUntilMs_ = millis() + kUiWarningDwellMs;
  } else {
    model_.rail = DisplayRailStyle::Static;
    model_.header = "Done";
    model_.primary = result.itemName.length() > 0 ? result.itemName : String("Inventory updated");
    // Negate in unsigned arithmetic: -INT_MIN overflows int (undefined behaviour).
    const int delta = result.appliedDelta;
    const unsigned long magnitude = delta < 0
        ? 0UL - static_cast<unsigned long>(delta)
        : static_cast<unsigned long>(delta);
    model_.secondary = String(delta >= 0 ? "Added " : "Removed ") +
                       String(magnitude) + " / total " + result.quantity;
    model_.tertiary = result.purposeLabel;
    model_.actions = "C Menu";
    resultUntilMs_ = millis() + kUiSuccessDwellMs;
  }
  markDirty();
}

bool DisplayController::showResult(const SyncResult& result) {
  if (model_.mode == DisplayUiMode::Quantity) {
    if (deferredResult_) return false;
    pendingResult_ = result;
    deferredResult_ = true;
    return true;
  }
  // Screens that need an explicit dismissal are never replaced by a result.
  if (model_.mode == DisplayUiMode::Warning || model_.mode == DisplayUiMode::Error) return false;
  applyResult(result);
  return true;
}

void DisplayController::showLocalFailure(const SyncResult& result) {
  // A scan that could not be stored must be visible: it preempts any deferred
  // or displayed result.
  deferredResult_ = false;
  applyResult(result);
}

bool DisplayController::finishInteraction() {
  if (deferredResult_) {
    deferredResult_ = false;
    applyResult(pendingResult_);
    return true;
  }
  showReady();
  return false;
}

void DisplayController::dismissError() {
  if (model_.mode == DisplayUiMode::Error || model_.mode == DisplayUiMode::Warning) showReady();
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
  clearContent();
  standby_ = false;
  renderer_.setPowerSave(false);
  model_.mode = DisplayUiMode::Ota;
  model_.rail = DisplayRailStyle::None;
  model_.showPcStatus = false;
  model_.primary = "Updating";
  model_.otaPercent = constrain(percent, 0, 100);
  // The i's dot carries the status, as in the desktop update wizard.
  if (text == "RESTARTING") {
    model_.brandDot = DisplayBrandDot::Full;
    model_.secondary = "Restarting";
  } else if (text == "UPDATING") {
    model_.brandDot = DisplayBrandDot::Progress;
    model_.secondary = String(model_.otaPercent) + "%";
  } else {
    model_.brandDot = DisplayBrandDot::Busy;
    model_.secondary = "Preparing";
  }
  markDirty();
}

void DisplayController::showSetup(uint32_t pairingCode, const String& status) {
  clearContent();
  standby_ = false;
  renderer_.setPowerSave(false);
  model_.mode = DisplayUiMode::Setup;
  model_.showPcStatus = false;
  model_.setupCode = pairingCode;
  if (status == "BLE READY") {
    model_.bodyKind = DisplayBodyKind::Setup;
    model_.rail = DisplayRailStyle::None;
    model_.header = "Pair this scanner";
    model_.primary = "Open Inventatory on PC";
    model_.secondary = "Settings / Inventatory Scan";
  } else if (status == "PAIRING LOCKED") {
    model_.bodyKind = DisplayBodyKind::Result;
    model_.rail = DisplayRailStyle::Static;
    model_.header = "Pairing locked";
    model_.primary = "Press A to try again";
    model_.secondary = "Only if you started it";
  } else if (status == "CONNECTING WIFI") {
    model_.bodyKind = DisplayBodyKind::Task;
    model_.rail = DisplayRailStyle::Activity;
    model_.header = "Pairing";
    model_.primary = "Connecting to Wi-Fi";
    model_.secondary = "Keep Inventatory open";
  } else {
    model_.bodyKind = DisplayBodyKind::Result;
    model_.rail = DisplayRailStyle::Static;
    model_.header = "Pairing failed";
    model_.primary = status == "SAVE FAILED" ? "Could not save setup" : "Could not connect";
    model_.secondary = status == "WIFI FAILED" ? "Check Wi-Fi details" : "Try pairing again";
  }
  markDirty();
}

void DisplayController::showMenu(bool blocked, bool unreadable) {
  clearContent();
  model_.mode = DisplayUiMode::Menu;
  model_.bodyKind = DisplayBodyKind::List;
  model_.rail = DisplayRailStyle::None;
  model_.showPcStatus = false;
  model_.header = "Menu";
  appendListRow(model_, "1  Quick labels");
  appendListRow(model_, "2  Device settings");
  if (unreadable) {
    appendListRow(model_, "3  Repair scan queue");
  } else if (blocked) {
    appendListRow(model_, "3  Discard stuck scan");
  }
  model_.actions = "C Back";
  markDirty();
}

void DisplayController::showQuickLabels(const String* labels, std::size_t count, std::size_t page,
                                        bool snapshotReceived, bool pcSupportsQuickLabels) {
  clearContent();
  model_.mode = DisplayUiMode::QuickLabels;
  model_.header = "Quick labels";
  model_.showPcStatus = true;
  model_.actions = "C Back";
  if (!snapshotReceived) {
    model_.bodyKind = DisplayBodyKind::Task;
    model_.rail = DisplayRailStyle::Activity;
    model_.primary = "Getting labels";
    model_.secondary = "From your PC";
  } else if (!pcSupportsQuickLabels) {
    model_.bodyKind = DisplayBodyKind::Task;
    model_.rail = DisplayRailStyle::Static;
    model_.primary = "Update Inventatory";
    model_.secondary = "Quick labels unavailable";
  } else if (count == 0) {
    model_.bodyKind = DisplayBodyKind::Task;
    model_.rail = DisplayRailStyle::Static;
    model_.primary = "No labels yet";
    model_.secondary = "Add them on your PC";
  } else {
    model_.bodyKind = DisplayBodyKind::List;
    model_.rail = DisplayRailStyle::None;
    const std::size_t pages = (count + kQuickLabelsPerPage - 1U) / kQuickLabelsPerPage;
    model_.pageText = String(page + 1U) + "/" + pages;
    const auto first = page * kQuickLabelsPerPage;
    for (std::size_t index = 0; index < kQuickLabelsPerPage && first + index < count; ++index) {
      appendListRow(model_, String(index + 1U) + "  " + labels[first + index]);
    }
    if (pages > 1U) model_.actions = "D Next C Back";
  }
  markDirty();
}

void DisplayController::showQuickSettings(uint8_t contrast) {
  clearContent();
  model_.mode = DisplayUiMode::QuickSettings;
  model_.bodyKind = DisplayBodyKind::List;
  model_.rail = DisplayRailStyle::None;
  model_.showPcStatus = false;
  model_.header = "Device settings";
  appendListRow(model_, "1  Display contrast  " + String(contrast));
  appendListRow(model_, "2  Standby");
  appendListRow(model_, "3  Pair with new PC");
  appendListRow(model_, "4  Software update");
  model_.actions = "C Back";
  markDirty();
}

void DisplayController::showContrast(uint8_t contrast) {
  clearContent();
  model_.mode = DisplayUiMode::Contrast;
  model_.bodyKind = DisplayBodyKind::Contrast;
  model_.rail = DisplayRailStyle::None;
  model_.showPcStatus = false;
  model_.header = "Display contrast";
  model_.primary = String(contrast);
  model_.actions = "A - B + C Cancel";
  markDirty();
}

void DisplayController::showResetConfirm() {
  clearContent();
  model_.mode = DisplayUiMode::ConfirmReset;
  model_.bodyKind = DisplayBodyKind::Confirm;
  model_.rail = DisplayRailStyle::None;
  model_.showPcStatus = false;
  model_.header = "Pair with new PC?";
  model_.primary = "Saved Wi-Fi is erased";
  model_.secondary = "Queued scans are kept";
  model_.actions = "A Confirm C Cancel";
  markDirty();
}

void DisplayController::showDiscardConfirm() {
  clearContent();
  model_.mode = DisplayUiMode::ConfirmReset;
  model_.bodyKind = DisplayBodyKind::Confirm;
  model_.rail = DisplayRailStyle::None;
  model_.showPcStatus = false;
  model_.header = "Skip stuck scan?";
  model_.primary = "Oldest queued scan";
  model_.secondary = "It is deleted for good";
  model_.actions = "A Discard C Cancel";
  markDirty();
}

void DisplayController::showRepairConfirm() {
  clearContent();
  model_.mode = DisplayUiMode::ConfirmReset;
  model_.bodyKind = DisplayBodyKind::Confirm;
  model_.rail = DisplayRailStyle::None;
  model_.showPcStatus = false;
  model_.header = "Repair scan queue?";
  model_.primary = "Unreadable scans are lost";
  model_.secondary = "Readable ones are kept";
  model_.actions = "A Repair C Cancel";
  markDirty();
}

void DisplayController::showPrinting(const String& label) {
  clearContent();
  model_.mode = DisplayUiMode::Printing;
  model_.bodyKind = DisplayBodyKind::Task;
  model_.rail = DisplayRailStyle::Activity;
  model_.showPcStatus = true;
  model_.header = "Printing label";
  model_.primary = label;
  model_.secondary = "Waiting for printer";
  model_.actions = "C Cancel";
  markDirty();
}

void DisplayController::showQuickLabelPrintResult(bool success, const String& message) {
  clearContent();
  model_.mode = success ? DisplayUiMode::Success : DisplayUiMode::Error;
  model_.bodyKind = DisplayBodyKind::Result;
  model_.rail = DisplayRailStyle::Static;
  model_.showPcStatus = true;
  model_.header = success ? "Done" : "Print failed";
  model_.primary = success ? String("Label sent") : message;
  model_.secondary = success ? message : String("Check it on your PC");
  model_.actions = success ? String() : String("C Dismiss");
  resultUntilMs_ = success ? millis() + kUiSuccessDwellMs : 0;
  markDirty();
}

RenderActivity DisplayController::computeActivity(unsigned long now) const {
  RenderActivity activity;
  if (uiActivityEnabled(standby_, model_.rail == DisplayRailStyle::Activity)) {
    activity.alternate = uiActivityAlternate(now);
    activity.railActivityOffset = uiRailSweepOffset(
        now, style::kRailActivityStepMs, style::kRailActivityTravel,
        style::kRailActivityStepsPerLeg);
  }
  if (!standby_ && model_.mode == DisplayUiMode::Quantity) {
    activity.quantityCursorVisible = !uiActivityAlternate(now);
  }
  if (!standby_) {
    switch (model_.brandDot) {
      case DisplayBrandDot::Full: activity.brandDotSlots = kBrandDotFull; break;
      case DisplayBrandDot::Busy: activity.brandDotSlots = brandDotBusy(now); break;
      case DisplayBrandDot::Progress:
        activity.brandDotSlots = brandDotProgress(model_.otaPercent, now);
        break;
      case DisplayBrandDot::Fault: activity.brandDotSlots = kBrandDotFault; break;
    }
  }
  return activity;
}

bool DisplayController::activityEquals(const RenderActivity& a, const RenderActivity& b) {
  return a.alternate == b.alternate && a.quantityCursorVisible == b.quantityCursorVisible &&
         a.railActivityOffset == b.railActivityOffset && a.brandDotSlots == b.brandDotSlots;
}

void DisplayController::tick() {
  const auto now = millis();
  if (uiDeadlineReached(now, resultUntilMs_)) {
    resultUntilMs_ = 0;
    if (uiResultDwellMayExpire(model_.mode == DisplayUiMode::Success ||
                               model_.mode == DisplayUiMode::Warning)) {
      showReady();
    }
  }
  const RenderActivity activity = computeActivity(now);
  if (!activityEquals(activity, lastActivity_)) dirty_ = true;
  if (!dirty_) {
    if (!standby_ && now - lastStartLineRecoveryMs_ >= hardware::kLcdStartLineRecoveryIntervalMs) {
      renderer_.normalizeStartLine();
      lastStartLineRecoveryMs_ = now;
    }
    return;
  }
  lastActivity_ = activity;
  renderer_.render(model_, activity);
  dirty_ = false;
  lastStartLineRecoveryMs_ = now;
}

}  // namespace inventatory_scan
