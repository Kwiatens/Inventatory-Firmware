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
  // '*' held for the warning time: the power-off countdown starts.
  PowerOffWarning,
  // Released, or joined by D or #, before the power-off commits.
  PowerOffCancel,
  // '*' held to the commit time. The chip sleeps once '*' is released.
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

// '*' (row 4, column 1) is the only key that can wake the chip from deep sleep.
// True while '*' is closed at the last scan.
bool keypadPowerKeyDown();
// Prepares the matrix for deep sleep. False, with the matrix restored for scanning, if '*' is
// still closed. True: the next deep sleep wakes on '*' only.
bool keypadArmPowerKeyWake();
