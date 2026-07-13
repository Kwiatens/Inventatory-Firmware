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
};

enum class DisplayConnectivityState {
  Offline,
  WifiConnecting,
  PcSearching,
  Syncing,
  Online,
};

struct DisplayModel {
  DisplayUiMode mode = DisplayUiMode::Boot;
  bool wifiOnline = false;
  bool pcOnline = false;
  bool supplierQuantity = false;
  std::size_t queueDepth = 0;
  DisplayConnectivityState connectivity = DisplayConnectivityState::Offline;
  bool headerAlternate = false;
  String quantityText = "1";
  std::size_t quantityCursor = 0;
  int otaPercent = 0;
  uint32_t setupCode = 0;
  String title;
  String line1;
  String line2;
  String footer;
};

}  // namespace hims_scan
