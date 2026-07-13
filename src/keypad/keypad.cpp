#include <Arduino.h>
#include <Keypad.h>

#include "config/config.h"
#include "keypad.h"

namespace {

constexpr unsigned int kKeyHoldTimeMs = 1800U;

enum class HoldGesture {
  None,
  Power,
  Ota,
};

HoldGesture holdGesture_ = HoldGesture::None;

bool isActiveState(KeyState state) {
  return state == PRESSED || state == HOLD;
}

bool isHeldState(KeyState state) {
  return state == HOLD;
}

int findKeyIndexForChar(Keypad& keypadRef, char keyChar) {
  for (byte i = 0; i < keypadRef.numKeys(); ++i) {
    if (keypadRef.key[i].kchar == keyChar) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

}  // namespace

static char KeyTable[4][4] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

static byte KEYPAD_ROW_PINS[4] = {
  KEYPAD_ROW_1_PIN,
  KEYPAD_ROW_2_PIN,
  KEYPAD_ROW_3_PIN,
  KEYPAD_ROW_4_PIN
};

static byte KEYPAD_COLUMN_PINS[4] = {
  KEYPAD_COLUMN_1_PIN,
  KEYPAD_COLUMN_2_PIN,
  KEYPAD_COLUMN_3_PIN,
  KEYPAD_COLUMN_4_PIN
};

static Keypad keypad = Keypad(makeKeymap(KeyTable), KEYPAD_ROW_PINS, KEYPAD_COLUMN_PINS, 4, 4);

void keypadInit() {
  keypad.setDebounceTime(20);
  keypad.setHoldTime(kKeyHoldTimeMs);

  Serial.print("Keypad rows: ");
  for (byte i = 0; i < 4; i++) {
    if (i > 0) {
      Serial.print(", ");
    }
    Serial.print(KEYPAD_ROW_PINS[i]);
  }
  Serial.print(" | cols: ");
  for (byte i = 0; i < 4; i++) {
    if (i > 0) {
      Serial.print(", ");
    }
    Serial.print(KEYPAD_COLUMN_PINS[i]);
  }
  Serial.println();
}

bool keypadPoll(HimsKeyEvent& event) {
  event = {};
  if (!keypad.getKeys()) {
    return false;
  }

  const auto keyCount = keypad.numKeys();
  const int starIndex = findKeyIndexForChar(keypad, '*');
  const int dIndex = findKeyIndexForChar(keypad, 'D');
  const bool starActive = starIndex >= 0 && isActiveState(keypad.key[starIndex].kstate);
  const bool dActive = dIndex >= 0 && isActiveState(keypad.key[dIndex].kstate);
  const bool starHeld = starIndex >= 0 && isHeldState(keypad.key[starIndex].kstate);
  const bool dHeld = dIndex >= 0 && isHeldState(keypad.key[dIndex].kstate);

  if (holdGesture_ == HoldGesture::Ota) {
    if (!starActive || !dActive) {
      holdGesture_ = HoldGesture::None;
    }
    return false;
  }

  if (holdGesture_ == HoldGesture::Power) {
    if (!dActive) {
      holdGesture_ = HoldGesture::None;
    }
    return false;
  }

  for (byte i = 0; i < keyCount; ++i) {
    const auto& keyState = keypad.key[i];
    if (!keyState.stateChanged || keyState.kstate != PRESSED) {
      continue;
    }

    const char key = keyState.kchar;
    event.value = key;
    if (key >= '0' && key <= '9') {
      event.type = HimsKeyEventType::Digit;
      return true;
    }
    if (key == 'A') {
      event.type = HimsKeyEventType::Add;
      return true;
    }
    if (key == 'B') {
      event.type = HimsKeyEventType::Subtract;
      return true;
    }
    if (key == 'C') {
      event.type = HimsKeyEventType::Cancel;
      return true;
    }
  }

  if (starActive && dActive && (starHeld || dHeld)) {
    holdGesture_ = HoldGesture::Ota;
    event.type = HimsKeyEventType::OtaUpdate;
    event.value = 'D';
    return true;
  }

  if (dHeld && !starActive) {
    holdGesture_ = HoldGesture::Power;
    event.type = HimsKeyEventType::PowerToggle;
    event.value = 'D';
    return true;
  }

  return false;
}

