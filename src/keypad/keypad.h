#pragma once

#include <Arduino.h>

enum class HimsKeyEventType {
  None,
  Digit,
  Add,
  Subtract,
  Cancel,
  CycleCursor,
  PowerToggle,
  OtaUpdate,
  ProvisioningReset,
  Other,
};

struct HimsKeyEvent {
  HimsKeyEventType type = HimsKeyEventType::None;
  char value = 0;
};

void keypadInit();
bool keypadPoll(HimsKeyEvent& event);
