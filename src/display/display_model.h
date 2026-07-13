#pragma once

#include <Arduino.h>

namespace hims_scan {

enum class DisplayUiMode {
  Boot,
  Ready,
  Quantity,
  Queued,
  Success,
  Warning,
  Error,
  Standby,
  Ota,
};

struct DisplayModel {
  DisplayUiMode mode = DisplayUiMode::Boot;
  bool wifiOnline = false;
  bool pcOnline = false;
  bool supplierQuantity = false;
  std::size_t queueDepth = 0;
  int quantity = 1;
  int otaPercent = 0;
  String title;
  String line1;
  String line2;
  String footer;
};

}  // namespace hims_scan
