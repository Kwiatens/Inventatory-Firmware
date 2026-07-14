#pragma once

#include <Arduino.h>

namespace hims_scan {

enum class DisplayUiMode {
  Splash,
  Boot,
  Ready,
  Quantity,
  Queued,
  Success,
  Warning,
  Error,
  Standby,
  Ota,
  Setup,
  Menu,
  QuickLabels,
  QuickSettings,
  Contrast,
  ConfirmReset,
  Printing,
};

enum class DisplayConnectivityState {
  Offline,
  WifiConnecting,
  PcSearching,
  Syncing,
  Online,
};

// Per-frame animation state computed by the controller and consumed by the
// renderer. Kept separate from DisplayModel because it changes every tick while
// an animation is live, whereas DisplayModel only changes on real state changes.
struct RenderAnim {
  bool pulseActive = false;    // scan-registered sweep window is live
  uint8_t pulsePhase = 0;      // 0..kPulsePhases-1, sweep position down the gutter
  uint8_t spinnerFrame = 0;    // 0..7, rotating waiting spinner
  uint8_t successProgress = 0; // 0..100, checkmark draw-in
};

struct DisplayModel {
  DisplayUiMode mode = DisplayUiMode::Boot;
  bool wifiOnline = false;
  bool pcOnline = false;
  bool supplierQuantity = false;
  std::size_t queueDepth = 0;
  DisplayConnectivityState connectivity = DisplayConnectivityState::Offline;
  String quantityText = "1";
  std::size_t quantityCursor = 0;
  int otaPercent = 0;
  uint32_t setupCode = 0;
  String title;
  String line1;
  String line2;
  String footer;
  String menuLines[5];
  std::size_t menuLineCount = 0;
};

}  // namespace hims_scan
