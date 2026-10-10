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
  // Builds the splash lockup step by step; blocks for about 1.5 s.
  void playBootIntro();
  void showBoot(const String& stage);
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
  void showQuickSettings(uint8_t contrast);
  void showContrast(uint8_t contrast);
  void showResetConfirm();
  void showDiscardConfirm();
  void showRepairConfirm();
  void showPrinting(const String& label);
  void showQuickLabelPrintResult(bool success, const String& message);
  void setSyncBlocked(bool blocked);
  void setQueueUnreadable(bool unreadable);
  bool errorVisible() const {
    return model_.mode == DisplayUiMode::Error || model_.mode == DisplayUiMode::Warning;
  }

 private:
  void markDirty();
  void applyResult(const SyncResult& result);
  void clearContent();
  void refreshReadyContent();
  RenderActivity computeActivity(unsigned long now) const;
  static bool activityEquals(const RenderActivity& a, const RenderActivity& b);

  DisplayRenderer renderer_;
  DisplayModel model_;
  bool dirty_ = true;
  bool deferredResult_ = false;
  SyncResult pendingResult_;
  unsigned long resultUntilMs_ = 0;
  RenderActivity lastActivity_;
  unsigned long lastStartLineRecoveryMs_ = 0;
  bool standby_ = false;
  bool syncBlocked_ = false;
  bool queueUnreadable_ = false;
};

}  // namespace inventatory_scan
