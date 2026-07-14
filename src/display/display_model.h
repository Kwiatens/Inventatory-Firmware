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

enum class DisplayBodyKind {
  Task,
  Quantity,
  Result,
  List,
  Contrast,
  Confirm,
  Setup,
  Progress,
};

enum class DisplayRailStyle {
  None,
  Thin,
  Solid,
  Warning,
  Error,
  Activity,
};

enum class QuantityLookupState {
  Pending,
  Found,
  NotFound,
  Unavailable,
};

struct RenderActivity {
  bool alternate = false;
  bool quantityCursorVisible = true;
};

struct DisplayModel {
  DisplayUiMode mode = DisplayUiMode::Boot;
  DisplayBodyKind bodyKind = DisplayBodyKind::Task;
  DisplayRailStyle rail = DisplayRailStyle::Activity;
  DisplayConnectivityState connectivity = DisplayConnectivityState::Offline;
  bool showPcStatus = true;
  bool supplierQuantity = false;
  std::size_t queueDepth = 0;
  String header;
  String primary;
  String secondary;
  String tertiary;
  String actions;
  String pageText;
  String quantityPrompt;
  String quantityText = "1";
  std::size_t quantityCursor = 0;
  int otaPercent = 0;
  uint32_t setupCode = 0;
  String listRows[5];
  std::size_t listRowCount = 0;
};

}  // namespace hims_scan
