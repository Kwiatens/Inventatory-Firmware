#pragma once

#include "HimsScanSync.h"
#include "display/display_model.h"
#include "display/display_renderer.h"

namespace hims_scan {

class DisplayController {
 public:
  void begin();
  void tick();
  void setConnectivity(bool wifiOnline, bool pcOnline, std::size_t queueDepth);
  void showBoot(const String& stage);
  void showReady();
  void showQuantity(const String& code, int quantity, bool supplier);
  void updateQuantity(int quantity);
  void pulseScan();
  void showQueued();
  bool showResult(const SyncResult& result);
  bool finishInteraction();
  void dismissError();
  void setStandby(bool standby);
  void showOta(const String& text, int percent = 0);
  bool errorVisible() const { return model_.mode == DisplayUiMode::Error; }

 private:
  void markDirty();
  void applyResult(const SyncResult& result);

  DisplayRenderer renderer_;
  DisplayModel model_;
  bool dirty_ = true;
  bool deferredResult_ = false;
  SyncResult pendingResult_;
  unsigned long pulseUntilMs_ = 0;
  unsigned long resultUntilMs_ = 0;
  bool lastPulseFrame_ = false;
};

}  // namespace hims_scan
