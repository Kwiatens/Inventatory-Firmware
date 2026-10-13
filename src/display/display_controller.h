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
  void setConnectivity(DisplayConnectivityState state, bool wifiOnline, bool pcOnline,
                       std::size_t queueDepth);
  void showSplash();
  // Shows the splash for holdMs, with the mark blooming in first, and keeps the LCD refreshed meanwhile.
  void playBootSplash(unsigned long holdMs);
  void showBoot(const String& stage);
  // The soft power-off warning: a countdown bar overlay. The bar drains by itself from the moment it appears.
  void showPowerWarning();
  // Runs the power-off ending to its end: the mark sinks behind the glass, the text rests, and the
  // panel goes dark. Returns with the panel blank, before the chip sleeps.
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
  void showQuickLabels(const String* labels, std::size_t count, std::size_t page, bool snapshotReceived,
                       bool pcSupportsQuickLabels);
  void showQuickSettings();
  void showDisplaySettings(uint8_t contrast, const String& backlightLabel);
  void showBacklight(const String& label, const String& detail, std::size_t index, std::size_t count);
  void showContrast(uint8_t contrast);
  void showResetConfirm();
  void showDiscardConfirm();
  void showRepairConfirm(bool everyScanLost = false);
  void showPrinting(const String& label);
  void showQuickLabelPrintResult(bool success, const String& message);
  void setSyncBlocked(bool blocked);
  void setQueueUnreadable(bool unreadable);

  // Battery in the top bar. percent < 0 means unknown.
  void setBatteryStatus(int percent, bool charging, const String& estimate);
  // Overlays cover the screen beneath them and leave it intact, so the screen is
  // back as it was when the overlay ends.
  void showBatteryCard(bool charging, int percent, const String& estimate, unsigned long dwellMs);
  // countdownSeconds < 0 hides the countdown line and bar.
  void showLowBattery(const String& title, const String& detail, const String& note, int countdownSeconds,
                      int countdownPercent);
  void clearOverlay();

  bool errorVisible() const {
    return model_.mode == DisplayUiMode::Error || model_.mode == DisplayUiMode::Warning;
  }

 private:
  void markDirty();
  void applyResult(const SyncResult& result);
  void clearContent();
  void refreshReadyContent();
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
  // The boot mark's bloom stage while the splash plays; the full mark otherwise.
  int bootBloomStage_ = kBootBloomStages;
};

}  // namespace inventatory_scan
