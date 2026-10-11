// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <Arduino.h>

enum class InventatoryKeyEventType {
  None,
  Digit,
  Add,
  Subtract,
  Cancel,
  CycleCursor,
  DeleteDigit,
  PowerToggle,
  PowerOff,
  OtaUpdate,
  ProvisioningReset,
  Other,
};

struct InventatoryKeyEvent {
  InventatoryKeyEventType type = InventatoryKeyEventType::None;
  char value = 0;
};

void keypadInit();
bool keypadPoll(InventatoryKeyEvent& event);

// Power key: '*' (row 4, column 1) is the only key that can wake the chip from deep sleep.
// True while '*' is closed, as of the last scan.
bool keypadPowerKeyDown();
// Prepares the matrix for deep sleep. Returns false, with the matrix restored for scanning,
// if '*' is still closed. On true the next deep sleep wakes on '*' only.
bool keypadArmPowerKeyWake();
