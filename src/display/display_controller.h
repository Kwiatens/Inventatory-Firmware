#pragma once

#include "HimsScanSync.h"
#include "display/display_model.h"
#include "display/display_renderer.h"

namespace hims_scan {

class DisplayController {
 public:
  void begin();
  void tick();
  void setConnectivity(DisplayConnectivityState state, bool wifiOnline, bool pcOnline,
                       std::size_t queueDepth);
  void showSplash();
  void showBoot(const String& stage);
  void showReady();
  void showQuantity(const String& itemText, const String& quantityText,
                    std::size_t cursor, bool supplier);
  void updateQuantity(const String& quantityText, std::size_t cursor);
  void updateQuantityItem(const String& itemText);
  void pulseScan();
  void showQueued();
  bool showResult(const SyncResult& result);
  bool finishInteraction();
  void dismissError();
  void setStandby(bool standby);
  void setContrast(uint8_t contrast);
  void showOta(const String& text, int percent = 0);
  void showSetup(uint32_t pairingCode, const String& status = "BLE READY");
  void showMenu();
  void showQuickLabels(const String* labels, std::size_t count, std::size_t page, bool snapshotReceived,
                       bool pcSupportsQuickLabels);
  void showQuickSettings(uint8_t contrast);
  void showContrast(uint8_t contrast);
  void showResetConfirm();
  void showPrinting(const String& label);
  void showQuickLabelPrintResult(bool success, const String& message);
  bool errorVisible() const { return model_.mode == DisplayUiMode::Error; }

 private:
  void markDirty();
  void applyResult(const SyncResult& result);
  void clearMenuLines();

  DisplayRenderer renderer_;
  DisplayModel model_;
  bool dirty_ = true;
  bool deferredResult_ = false;
  SyncResult pendingResult_;
  unsigned long pulseUntilMs_ = 0;
  unsigned long resultUntilMs_ = 0;
  bool lastPulseFrame_ = false;
  unsigned long lastHeaderAlternateMs_ = 0;
  unsigned long lastStartLineRecoveryMs_ = 0;
  bool standby_ = false;
};

}  // namespace hims_scan
