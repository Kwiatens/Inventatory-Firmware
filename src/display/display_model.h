// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <Arduino.h>

#include "InventatoryScanUiPolicy.h"

namespace inventatory_scan {

enum class DisplayUiMode {
  Splash,
  Boot,
  Ready,
  Quantity,
  Success,
  Warning,
  Error,
  Standby,
  Ota,
  Setup,
  Menu,
  FaultDetail,
  QuickLabels,
  QuickSettings,
  Sending,
  Contrast,
  ConfirmReset,
  Printing,
  DisplaySettings,
  Backlight,
  EnergySaving,
  SleepAfter,
  PowerOffAfter,
  BatteryCard,
  CountdownWarning,
  PowerWarning,
  PowerOff,
  Blank,
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
  Fault,
  Contrast,
  Confirm,
  Setup,
  // A setting picker: one segment per option. The backlight and the energy saving delays use it.
  Choice,
};

// What the dot of the ›i mark shows on the boot and update screens. The normal
// boot shows no dot; a fault keeps a static broken dot.
enum class DisplayBrandDot {
  None,
  Full,
  Busy,
  Progress,
  Fault,
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
  uint8_t brandDotSlots = 0;
  // Top bar page, as a HeaderPage value.
  uint8_t headerPage = 0;
  // Battery card fill, 0 .. kBatteryCardFillSteps.
  uint8_t cardFillStep = 0;
  // Boot mark bloom stage, 0 .. kBootBloomStages. The full mark is kBootBloomStages.
  int bloomStage = kBootBloomStages;
  // Rows the mark has dropped behind the glass during the power-off sink.
  int markOffsetY = 0;
  // Filled width of the power-off countdown bar, in pixels.
  int powerBarFillPx = 0;
};

struct DisplayModel {
  DisplayUiMode mode = DisplayUiMode::Boot;
  DisplayBodyKind bodyKind = DisplayBodyKind::Task;
  DisplayRailStyle rail = DisplayRailStyle::Activity;
  DisplayConnectivityState connectivity = DisplayConnectivityState::Offline;
  bool showPcStatus = true;
  DisplayBrandDot brandDot = DisplayBrandDot::None;
  bool animateEllipsis = false;
  bool receiveQuantity = false;
  std::size_t queueDepth = 0;
  String header;
  String primary;
  String secondary;
  String tertiary;
  // The scanner's error code. The body draws it as "Error code" with the code in bold.
  String errorCode;
  String actions;
  String pageText;
  String quantityPrompt;
  String quantityText = "1";
  std::size_t quantityCursor = 0;
  int otaPercent = 0;
  uint32_t setupCode = 0;
  String listRows[5];
  std::size_t listRowCount = 0;
  // The scanner is not running. Not content, so clearContent() leaves it alone.
  bool scannerFault = false;
  // A standing problem of any kind is on show, so the top bar's warning mark is drawn. Not content either.
  bool problemMark = false;
  // Battery in the top bar and on the battery cards. batteryPercent < 0 means unknown.
  int batteryPercent = -1;
  bool batteryCharging = false;
  // Runtime line ("6h 20m left", "Full: 1h 05m"); empty until a rate is known.
  String batteryEstimate;
  // Low-battery countdown in seconds; negative when the screen has none.
  int countdownSeconds = -1;
  int countdownPercent = 0;
  // Setting picker: the chosen option out of choiceCount.
  std::size_t choiceIndex = 0;
  std::size_t choiceCount = 0;
};

}  // namespace inventatory_scan
