#pragma once

#include <Arduino.h>

enum class HimsKeyEventType {
  None,
  Digit,
  Add,
  Subtract,
  Cancel,
  PowerToggle,
  OtaUpdate,
  Other,
};

struct HimsKeyEvent {
  HimsKeyEventType type = HimsKeyEventType::None;
  char value = 0;
};

void keypadInit();
bool keypadPoll(HimsKeyEvent& event);
