// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#include "display/display_controller.h"

#include <algorithm>
#include <cstring>

#include "InventatoryScanErrorCodes.h"
#include "InventatoryScanUiPolicy.h"
#include "config/hardware.h"
#include "config/version.h"
#include "display/display_style.h"

namespace inventatory_scan {

namespace {

// Rows the list body can show; anything beyond this is never drawn.
constexpr std::size_t kListRowLimit = style::kListMaxRows;
static_assert(kListRowLimit <= sizeof(DisplayModel::listRows) / sizeof(DisplayModel::listRows[0]),
              "list row limit exceeds DisplayModel::listRows capacity");
static_assert(kQuickLabelsPerPage <= kListRowLimit, "a quick-label page must fit the list");

// Appends a row, or drops it (returns false) when the list is full.
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
  // Cancels any pending result dwell; result setters re-arm it. Battery fields are kept.
  resultUntilMs_ = 0;
  model_.animateEllipsis = false;
  model_.otaProgress = false;
  model_.countdownSeconds = -1;
  model_.countdownPercent = 0;
  model_.choiceIndex = 0;
  model_.choiceCount = 0;
  model_.header.clear();
  model_.primary.clear();
  model_.secondary.clear();
  model_.tertiary.clear();
  model_.actions.clear();
  model_.pageText.clear();
  model_.quantityPrompt.clear();
  model_.listRowCount = 0;
  model_.errorCode.clear();
  for (auto& row : model_.listRows) row.clear();
}

// Standing problem codes, in screen rotation order.
std::size_t DisplayController::standingProblemCodes(const char* (&codes)[kStandingProblemCount]) const {
  std::size_t count = 0;
  if (model_.scannerFault) codes[count++] = kScannerFaultCode;
  if (queueUnreadable_) codes[count++] = kQueueUnreadableCode;
  if (syncBlocked_) codes[count++] = kSyncBlockedCode;
  if (batteryUnreadable_) codes[count++] = kBatteryUnreadableCode;
  return count;
}

bool DisplayController::hasProblem() const {
  return model_.scannerFault || queueUnreadable_ || syncBlocked_ || batteryUnreadable_;
}

void DisplayController::refreshReadyContent() {
  model_.header = "Inventatory";
  const char* codes[kStandingProblemCount];
  const std::size_t problems = standingProblemCodes(codes);
  // A standing problem blocks scanning: show its rotating code in place of the ready text.
  if (problems > 0) {
    model_.primary = "There is a problem";
    model_.secondary.clear();
    model_.errorCode = codes[faultRotationIndex(millis(), problems, kFaultRotateMs)];
  } else {
    model_.errorCode.clear();
    model_.primary = "Ready to scan";
    if (displayQueueDepth(model_.queueDepth)) {
      model_.secondary = String(model_.queueDepth) + " scans safely queued";
    } else if (model_.connectivity == DisplayConnectivityState::Syncing) {
      model_.secondary = "Sending saved work";
    } else if (updateAvailableShown()) {
      model_.secondary = "Update available";
    } else if (model_.connectivity == DisplayConnectivityState::Online) {
      model_.secondary = "Point at a label";
    } else {
      model_.secondary = "Scans save on device";
    }
  }
  model_.rail = model_.connectivity == DisplayConnectivityState::Syncing
                    ? DisplayRailStyle::Activity
                    : DisplayRailStyle::Static;
  // The # key explains a problem, from the main screen and the menu.
  if (problems > 0) {
    model_.actions = "# Fault info C Menu";
  } else if (updateAvailableShown()) {
    model_.actions = "D Update C Menu";
  } else {
    model_.actions = "C Menu";
  }
}

void DisplayController::setUpdateAvailable(bool available) {
  if (updateAvailable_ == available) return;
  updateAvailable_ = available;
  if (model_.mode == DisplayUiMode::Ready) refreshReadyContent();
  markDirty();
}

// Only while the desktop is reachable and nothing more pressing holds the line.
bool DisplayController::updateAvailableShown() const {
  return updateAvailable_ && !hasProblem() && !displayQueueDepth(model_.queueDepth) &&
         model_.connectivity == DisplayConnectivityState::Online;
}

void DisplayController::setSyncBlocked(bool blocked) {
  if (syncBlocked_ == blocked) return;
  syncBlocked_ = blocked;
  model_.problemMark = hasProblem();
  if (model_.mode == DisplayUiMode::Ready) refreshReadyContent();
  markDirty();
}

void DisplayController::setQueueUnreadable(bool unreadable) {
  if (queueUnreadable_ == unreadable) return;
  queueUnreadable_ = unreadable;
  model_.problemMark = hasProblem();
  if (model_.mode == DisplayUiMode::Ready) refreshReadyContent();
  markDirty();
}

void DisplayController::setBatteryUnreadable(bool unreadable) {
  if (batteryUnreadable_ == unreadable) return;
  batteryUnreadable_ = unreadable;
  model_.problemMark = hasProblem();
  if (model_.mode == DisplayUiMode::Ready) refreshReadyContent();
  markDirty();
}

void DisplayController::setConnectivity(DisplayConnectivityState state, std::size_t queueDepth) {
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

// Boot splash held for holdMs; the mark blooms in, and tick() keeps the LCD refreshed.
void DisplayController::playBootSplash(unsigned long holdMs) {
  showSplash();
  const unsigned long start = millis();
  while (millis() - start < holdMs) {
    bootBloomStage_ = bootBloomStage(millis() - start);
    tick();
    delay(20);
  }
  bootBloomStage_ = kBootBloomStages;
  markDirty();
}

// Boot stage replaces the product name until the scanner is ready.
void DisplayController::showBoot(const String& stage, const String& code) {
  showSplash();
  model_.mode = DisplayUiMode::Boot;
  if (stage.length() == 0) return;
  model_.primary = stage;
  model_.secondary = code.length() > 0 ? code : String("See USB serial");
}

// Soft power-off warning: a countdown bar overlay; the screen beneath returns when it clears.
void DisplayController::showPowerWarning() {
  overlay_ = DisplayModel();
  overlay_.mode = DisplayUiMode::PowerWarning;
  overlay_.rail = DisplayRailStyle::None;
  overlay_.primary = "Powering off";
  overlay_.secondary = "Release * to cancel";
  overlayActive_ = true;
  overlayStartedAt_ = millis();
  overlayUntilMs_ = 0;
  markDirty();
}

// Plays the power-off ending to the end: the mark sinks behind the glass, then the panel goes dark.
// The caller sleeps the chip after this returns.
void DisplayController::playPowerOffSink() {
  clearOverlay();
  clearContent();
  model_.mode = DisplayUiMode::PowerOff;
  model_.rail = DisplayRailStyle::None;
  model_.showPcStatus = false;
  model_.primary = "Press * to wake";
  for (int step = 0; step < kPowerOffSinkSteps; ++step) {
    renderPowerOffFrame(powerOffSinkOffset(step));
    delay(step < kPowerOffSinkSteps - 1 ? kPowerOffSinkStepMs : kPowerOffRestMs);
  }
  model_.mode = DisplayUiMode::Blank;
  renderPowerOffFrame(0);
  dirty_ = false;
}

// Draws a power-off frame directly, outside tick(), since each step is timed.
void DisplayController::renderPowerOffFrame(int markOffsetY) {
  RenderActivity activity = computeActivity(millis());
  activity.markOffsetY = markOffsetY;
  renderer_.render(model_, activity);
}

void DisplayController::showReady() {
  clearContent();
  model_.mode = DisplayUiMode::Ready;
  model_.bodyKind = DisplayBodyKind::Task;
  model_.showPcStatus = true;
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
  model_.quantityPrompt = receiveMode ? "Add quantity:" : "Change by:";
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
  if (text == "RESTARTING") {
    model_.secondary = "Restarting";
  } else if (text == "UPDATING") {
    model_.otaProgress = true;
    model_.secondary = String(model_.otaPercent) + "%";
  } else {
    model_.secondary = "Preparing";
  }
  markDirty();
}

// The code a failed setup step shows under its message. A step with no code shows none.
static String setupFailureCode(const String& status) {
  if (status == "SAVE FAILED") return kSetupNotSavedCode;
  if (status == "WIFI FAILED") return kWifiNotJoinedCode;
  if (status == "NETWORK ERROR") return kNetworkStartFailedCode;
  return String();
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
    model_.tertiary = kPairingLockedCode;
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
    model_.tertiary = setupFailureCode(status);
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
  // The # key explains a problem; the menu offers it only while one stands.
  model_.actions = hasProblem() ? String("# Fault info C Back") : String("C Back");
  markDirty();
}

// Fault detail: the error code, then what it means and what to check.
void DisplayController::showFaultDetail() {
  const char* codes[kStandingProblemCount];
  const std::size_t problems = standingProblemCodes(codes);
  if (problems == 0) {
    showReady();
    return;
  }
  clearContent();
  model_.mode = DisplayUiMode::FaultDetail;
  model_.bodyKind = DisplayBodyKind::Fault;
  model_.rail = DisplayRailStyle::None;
  model_.showPcStatus = false;
  applyFaultDetail(codes[faultRotationIndex(millis(), problems, kFaultRotateMs)]);
  model_.actions = "C Back";
  markDirty();
}

// Explanation lines for one standing problem, under its code.
void DisplayController::applyFaultDetail(const char* code) {
  model_.errorCode = code;
  if (std::strcmp(code, kQueueUnreadableCode) == 0) {
    model_.header = "Queue unreadable";
    model_.secondary = "Queue cannot be read";
    model_.tertiary = "Menu, 3: repair it";
  } else if (std::strcmp(code, kSyncBlockedCode) == 0) {
    model_.header = "Sync blocked";
    model_.secondary = "PC keeps refusing";
    model_.tertiary = "Menu, 3: discard it";
  } else if (std::strcmp(code, kBatteryUnreadableCode) == 0) {
    model_.header = "Battery not read";
    model_.secondary = "Voltage not valid";
    model_.tertiary = "Check battery wiring";
  } else {
    model_.header = "Scanner fault";
    model_.secondary = "Scanner not replying";
    model_.tertiary = "Check cable, restart.";
  }
}

void DisplayController::setScannerFault(bool fault) {
  if (model_.scannerFault == fault) return;
  model_.scannerFault = fault;
  model_.problemMark = hasProblem();
  // The footer offers # only while there is a fault.
  if (model_.mode == DisplayUiMode::Ready) refreshReadyContent();
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

void DisplayController::showQuickSettings() {
  clearContent();
  model_.mode = DisplayUiMode::QuickSettings;
  model_.bodyKind = DisplayBodyKind::List;
  model_.rail = DisplayRailStyle::None;
  model_.showPcStatus = false;
  model_.header = "Device settings";
  appendListRow(model_, "1  Display");
  appendListRow(model_, "2  Energy");
  appendListRow(model_, "3  Pair with new PC");
  appendListRow(model_, "4  Software update");
  model_.actions = "C Back";
  markDirty();
}

void DisplayController::showDisplaySettings(uint8_t contrast, const String& backlightLabel) {
  clearContent();
  model_.mode = DisplayUiMode::DisplaySettings;
  model_.bodyKind = DisplayBodyKind::List;
  model_.rail = DisplayRailStyle::None;
  model_.showPcStatus = false;
  model_.header = "Display";
  appendListRow(model_, "1  Contrast  " + String(contrast));
  appendListRow(model_, "2  Backlight  " + backlightLabel);
  model_.actions = "C Back";
  markDirty();
}

void DisplayController::showBacklight(const String& label, const String& detail, std::size_t index,
                                      std::size_t count) {
  showChoice(DisplayUiMode::Backlight, "Backlight", label, detail, index, count);
}

// Energy, in pages as Quick labels are: the switch, the two delays and dock mode, then the battery capacity.
// Each delay opens its own picker, and the capacity is typed in.
void DisplayController::showEnergySaving(bool enabled, const String& sleepLabel, const String& powerOffLabel,
                                         bool dockMode, std::size_t page, const String& batteryLabel) {
  clearContent();
  model_.mode = DisplayUiMode::EnergySaving;
  model_.bodyKind = DisplayBodyKind::List;
  model_.rail = DisplayRailStyle::None;
  model_.showPcStatus = false;
  model_.header = "Energy";
  model_.pageText = String(page + 1U) + "/" + kEnergyPageCount;
  if (page == 0) {
    appendListRow(model_, String("1  Enabled  ") + (enabled ? "On" : "Off"));
    appendListRow(model_, "2  Sleep after  " + sleepLabel);
    appendListRow(model_, "3  Turn off after  " + powerOffLabel);
    appendListRow(model_, String("4  Dock mode  ") + (dockMode ? "On" : "Off"));
  } else {
    appendListRow(model_, "1  Battery  " + batteryLabel);
  }
  model_.actions = "D Next C Back";
  markDirty();
}

// The typed battery capacity: the value so far, and a line below it saying what the keys do.
void DisplayController::showBatteryCapacityEntry(const String& value, const String& detail) {
  clearContent();
  model_.mode = DisplayUiMode::BatteryCapacity;
  model_.bodyKind = DisplayBodyKind::Choice;
  model_.rail = DisplayRailStyle::None;
  model_.showPcStatus = false;
  model_.header = "Battery";
  model_.primary = value;
  model_.secondary = detail;
  model_.actions = "A Save C Back";
  markDirty();
}

void DisplayController::showSleepAfter(const String& label, const String& detail, std::size_t index,
                                       std::size_t count) {
  showChoice(DisplayUiMode::SleepAfter, "Sleep after", label, detail, index, count);
}

void DisplayController::showPowerOffAfter(const String& label, const String& detail, std::size_t index,
                                          std::size_t count) {
  showChoice(DisplayUiMode::PowerOffAfter, "Turn off after", label, detail, index, count);
}

void DisplayController::showChoice(DisplayUiMode mode, const char* header, const String& label,
                                   const String& detail, std::size_t index, std::size_t count) {
  clearContent();
  model_.mode = mode;
  model_.bodyKind = DisplayBodyKind::Choice;
  model_.rail = DisplayRailStyle::None;
  model_.showPcStatus = false;
  model_.header = header;
  model_.primary = label;
  model_.secondary = detail;
  model_.choiceIndex = index;
  model_.choiceCount = count;
  model_.actions = "A - B + C Back";
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

void DisplayController::showUpdateConfirm(const String& version) {
  clearContent();
  standby_ = false;
  renderer_.setPowerSave(false);
  model_.mode = DisplayUiMode::ConfirmReset;
  model_.bodyKind = DisplayBodyKind::Confirm;
  model_.rail = DisplayRailStyle::None;
  model_.showPcStatus = false;
  model_.header = "Install update?";
  model_.primary = String("Version ") + version;
  model_.secondary = "Takes about a minute";
  model_.actions = "A Install C Cancel";
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

void DisplayController::showRepairConfirm(bool everyScanLost) {
  clearContent();
  model_.mode = DisplayUiMode::ConfirmReset;
  model_.bodyKind = DisplayBodyKind::Confirm;
  model_.rail = DisplayRailStyle::None;
  model_.showPcStatus = false;
  model_.header = "Repair scan queue?";
  model_.primary = everyScanLost ? "Queued scans cannot be read" : "Unreadable scans are lost";
  model_.secondary = everyScanLost ? "Repair deletes them for good" : "Readable ones are kept";
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

void DisplayController::showQuickLabelPrintResult(bool success, const String& message, const String& code) {
  clearContent();
  model_.mode = success ? DisplayUiMode::Success : DisplayUiMode::Error;
  model_.bodyKind = DisplayBodyKind::Result;
  model_.rail = DisplayRailStyle::Static;
  model_.showPcStatus = true;
  model_.header = success ? "Done" : "Print failed";
  model_.primary = success ? String("Label sent") : message;
  // The code takes the hint's place; a long message wraps over the line a third line would use.
  model_.secondary = success ? message : (code.length() > 0 ? code : String("Check it on your PC"));
  model_.actions = success ? String() : String("C Dismiss");
  resultUntilMs_ = success ? millis() + kUiSuccessDwellMs : 0;
  markDirty();
}

void DisplayController::setBatteryStatus(int percent, bool charging, bool chargeDone, const String& estimate) {
  if (model_.batteryPercent == percent && model_.batteryCharging == charging &&
      model_.batteryChargeDone == chargeDone && model_.batteryEstimate == estimate) {
    return;
  }
  model_.batteryPercent = percent;
  model_.batteryCharging = charging;
  model_.batteryChargeDone = chargeDone;
  model_.batteryEstimate = estimate;
  markDirty();
}

void DisplayController::showBatteryCard(bool charging, bool chargeDone, int percent, const String& estimate,
                                        unsigned long dwellMs) {
  overlay_ = DisplayModel();
  overlay_.mode = DisplayUiMode::BatteryCard;
  overlay_.bodyKind = DisplayBodyKind::Task;
  overlay_.rail = DisplayRailStyle::None;
  overlay_.primary = !charging ? "On battery" : (chargeDone || percent >= 100) ? "Charged" : "Charging";
  overlay_.secondary = estimate.length() > 0 ? estimate : String("Estimating time");
  overlay_.batteryPercent = percent;
  overlay_.batteryCharging = charging;
  overlay_.batteryChargeDone = chargeDone;
  overlay_.batteryEstimate = estimate;
  overlayActive_ = true;
  overlayStartedAt_ = millis();
  overlayUntilMs_ = overlayStartedAt_ + dwellMs;
  markDirty();
}

void DisplayController::showCountdownWarning(const String& title, const String& detail, const String& note,
                                             int countdownSeconds, int countdownPercent) {
  overlay_ = DisplayModel();
  overlay_.mode = DisplayUiMode::CountdownWarning;
  overlay_.bodyKind = DisplayBodyKind::Task;
  overlay_.rail = DisplayRailStyle::None;
  overlay_.primary = title;
  overlay_.secondary = detail;
  overlay_.tertiary = note;
  overlay_.countdownSeconds = countdownSeconds;
  overlay_.countdownPercent = countdownPercent;
  overlayActive_ = true;
  overlayStartedAt_ = millis();
  overlayUntilMs_ = 0;
  markDirty();
}

void DisplayController::clearOverlay() {
  if (!overlayActive_) return;
  overlayActive_ = false;
  markDirty();
}

RenderActivity DisplayController::computeActivity(unsigned long now) const {
  const DisplayModel& shown = shownModel();
  RenderActivity activity;
  activity.bloomStage = bootBloomStage_;
  if (uiActivityEnabled(standby_, shown.rail == DisplayRailStyle::Activity)) {
    activity.alternate = uiActivityAlternate(now);
    activity.railActivityOffset = uiRailSweepOffset(
        now, style::kRailActivityStepMs, style::kRailActivityTravel,
        style::kRailActivityStepsPerLeg);
  }
  if (!standby_ && shown.mode == DisplayUiMode::Quantity) {
    activity.quantityCursorVisible = !uiActivityAlternate(now);
  }
  // Only the shown screen animates; the top bar is hidden under an overlay.
  if (!overlayActive_) {
    const bool hasEstimate = model_.batteryPercent >= 0 && model_.batteryEstimate.length() > 0;
    activity.headerPage = static_cast<uint8_t>(headerPageAt(now, hasEstimate));
  } else if (overlay_.mode == DisplayUiMode::BatteryCard) {
    activity.cardFillStep = batteryCardFillStep(now - overlayStartedAt_);
  } else if (overlay_.mode == DisplayUiMode::PowerWarning) {
    activity.powerBarFillPx = powerOffBarFillPx(now - overlayStartedAt_, style::kPowerBarFillW);
  }
  return activity;
}

bool DisplayController::activityEquals(const RenderActivity& a, const RenderActivity& b) {
  return a.alternate == b.alternate && a.quantityCursorVisible == b.quantityCursorVisible &&
         a.railActivityOffset == b.railActivityOffset &&
         a.headerPage == b.headerPage && a.cardFillStep == b.cardFillStep &&
         a.bloomStage == b.bloomStage && a.powerBarFillPx == b.powerBarFillPx;
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
  if (overlayActive_ && uiDeadlineReached(now, overlayUntilMs_)) {
    overlayActive_ = false;
    markDirty();
  }
  // Standing problems take turns on the Ready and fault screens, one code every kFaultRotateMs.
  const char* codes[kStandingProblemCount];
  const std::size_t problems = standingProblemCodes(codes);
  if (problems > 0 && (model_.mode == DisplayUiMode::Ready || model_.mode == DisplayUiMode::FaultDetail)) {
    const char* code = codes[faultRotationIndex(now, problems, kFaultRotateMs)];
    if (model_.errorCode != code) {
      if (model_.mode == DisplayUiMode::Ready) {
        refreshReadyContent();
      } else {
        applyFaultDetail(code);
      }
      markDirty();
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
  renderer_.render(shownModel(), activity);
  dirty_ = false;
  lastStartLineRecoveryMs_ = now;
}

}  // namespace inventatory_scan
