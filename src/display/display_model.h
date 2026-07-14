#pragma once

#include <Arduino.h>

namespace inventatory_scan {

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
  Sending,
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
  Static,
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
  int railActivityOffset = 0;
};

struct DisplayModel {
  DisplayUiMode mode = DisplayUiMode::Boot;
  DisplayBodyKind bodyKind = DisplayBodyKind::Task;
  DisplayRailStyle rail = DisplayRailStyle::Activity;
  DisplayConnectivityState connectivity = DisplayConnectivityState::Offline;
  bool showPcStatus = true;
  bool animateEllipsis = false;
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

}  // namespace inventatory_scan
