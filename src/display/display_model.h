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
};

// What the dot of the ›i mark shows on the boot and update screens.
enum class DisplayBrandDot {
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
};

struct DisplayModel {
  DisplayUiMode mode = DisplayUiMode::Boot;
  DisplayBodyKind bodyKind = DisplayBodyKind::Task;
  DisplayRailStyle rail = DisplayRailStyle::Activity;
  DisplayConnectivityState connectivity = DisplayConnectivityState::Offline;
  bool showPcStatus = true;
  DisplayBrandDot brandDot = DisplayBrandDot::Full;
  // Elements of the boot lockup built so far; the full lockup outside the intro.
  int introStep = kBrandIntroSteps;
  bool animateEllipsis = false;
  bool receiveQuantity = false;
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
