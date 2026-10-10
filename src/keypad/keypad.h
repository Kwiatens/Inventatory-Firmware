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
