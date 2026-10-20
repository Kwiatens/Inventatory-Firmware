// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "InventatoryScanSync.h"
#include "display/display_model.h"
#include "display/display_renderer.h"

namespace inventatory_scan {

class DisplayController {
 public:
  void begin();
  void tick();
  void setConnectivity(DisplayConnectivityState state, std::size_t queueDepth);
  void showSplash();
  // Splash for holdMs; the mark blooms in first and the LCD keeps refreshing.
  void playBootSplash(unsigned long holdMs);
  // Start-up stage; an error code replaces "See USB serial" under it.
  void showBoot(const String& stage, const String& code = String());
  // Soft power-off warning: a countdown bar overlay that drains by itself.
  void showPowerWarning();
  // Plays the power-off ending; returns with the panel blank, before the chip sleeps.
  void playPowerOffSink();
  void showReady();
  void showQuantity(const String& itemText, const String& itemCode, const String& quantityText,
                    std::size_t cursor, bool receiveMode, QuantityLookupState lookupState);
  void updateQuantity(const String& quantityText, std::size_t cursor);
  void updateQuantityLookup(const String& itemText, QuantityLookupState lookupState);
  void showSending();
  bool showResult(const SyncResult& result);
  void showLocalFailure(const SyncResult& result);
  bool finishInteraction();
  void dismissError();
  void setStandby(bool standby);
  void setContrast(uint8_t contrast);
  void showOta(const String& text, int percent = 0);
  void showSetup(uint32_t pairingCode, const String& status = "BLE READY");
  void showMenu(bool blocked = false, bool unreadable = false);
  // Fault screen for the problem on show, opened from #.
  void showFaultDetail();
  void setScannerFault(bool fault);
  void showQuickLabels(const String* labels, std::size_t count, std::size_t page, bool snapshotReceived,
                       bool pcSupportsQuickLabels);
  void showQuickSettings();
  void showDisplaySettings(uint8_t contrast, const String& backlightLabel);
  void showBacklight(const String& label, const String& detail, std::size_t index, std::size_t count);
  void showEnergySaving(bool enabled, const String& sleepLabel, const String& powerOffLabel, bool dockMode,
                        std::size_t page, const String& batteryLabel);
  void showBatteryCapacityEntry(const String& value, const String& detail);
  void showSleepAfter(const String& label, const String& detail, std::size_t index, std::size_t count);
  void showPowerOffAfter(const String& label, const String& detail, std::size_t index, std::size_t count);
  void showContrast(uint8_t contrast);
  void showResetConfirm();
  void showDiscardConfirm();
  void showRepairConfirm(bool everyScanLost = false);
  void showPrinting(const String& label);
  // A failed print shows its code under the message, when there is one.
  void showQuickLabelPrintResult(bool success, const String& message, const String& code = String());
  void setSyncBlocked(bool blocked);
  void setQueueUnreadable(bool unreadable);
  void setBatteryUnreadable(bool unreadable);

  // Battery in the top bar. percent < 0 means unknown.
  // chargeDone: cell full with the charger LED dark; the top bar reads "Charged".
  void setBatteryStatus(int percent, bool charging, bool chargeDone, const String& estimate);
  // An overlay leaves the screen beneath intact; it returns as it was when the overlay ends.
  void showBatteryCard(bool charging, bool chargeDone, int percent, const String& estimate, unsigned long dwellMs);
  // Warning with a countdown. countdownSeconds < 0 hides the countdown line and bar.
  void showCountdownWarning(const String& title, const String& detail, const String& note, int countdownSeconds,
                            int countdownPercent);
  void clearOverlay();

  bool errorVisible() const {
    return model_.mode == DisplayUiMode::Error || model_.mode == DisplayUiMode::Warning;
  }
  // True while the scanner, queue or sync blocks scanning.
  bool hasProblem() const;

 private:
  // Most standing problems at once: scanner, queue, sync and battery.
  static constexpr std::size_t kStandingProblemCount = 4;

  void markDirty();
  void applyResult(const SyncResult& result);
  void clearContent();
  void showChoice(DisplayUiMode mode, const char* header, const String& label, const String& detail,
                  std::size_t index, std::size_t count);
  void refreshReadyContent();
  // Fills codes with the standing problems in screen rotation order; returns how many.
  std::size_t standingProblemCodes(const char* (&codes)[kStandingProblemCount]) const;
  void applyFaultDetail(const char* code);
  const DisplayModel& shownModel() const { return overlayActive_ ? overlay_ : model_; }
  RenderActivity computeActivity(unsigned long now) const;
  void renderPowerOffFrame(int markOffsetY);
  static bool activityEquals(const RenderActivity& a, const RenderActivity& b);

  DisplayRenderer renderer_;
  DisplayModel model_;
  DisplayModel overlay_;
  bool overlayActive_ = false;
  unsigned long overlayStartedAt_ = 0;
  // 0 keeps the overlay until clearOverlay().
  unsigned long overlayUntilMs_ = 0;
  bool dirty_ = true;
  bool deferredResult_ = false;
  SyncResult pendingResult_;
  unsigned long resultUntilMs_ = 0;
  RenderActivity lastActivity_;
  unsigned long lastStartLineRecoveryMs_ = 0;
  bool standby_ = false;
  bool syncBlocked_ = false;
  bool queueUnreadable_ = false;
  bool batteryUnreadable_ = false;
  // The boot mark's bloom stage while the splash plays; the full mark otherwise.
  int bootBloomStage_ = kBootBloomStages;
};

}  // namespace inventatory_scan
