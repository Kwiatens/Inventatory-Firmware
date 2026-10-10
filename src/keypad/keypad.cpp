// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#include <Arduino.h>

#include "config/config.h"
#include "keypad.h"

namespace {

constexpr unsigned int kKeyHoldTimeMs = 1800U;

enum class HoldGesture {
  None,
  Power,
  Ota,
  ProvisioningReset,
};

HoldGesture holdGesture_ = HoldGesture::None;
bool dShortPressPending_ = false;
bool hashShortPressPending_ = false;

enum KeyState { IDLE, PRESSED, HOLD, RELEASED };

constexpr byte kRows = 4;
constexpr byte kColumns = 4;
constexpr byte kKeyCount = kRows * kColumns;
constexpr unsigned long kScanIntervalMs = 20UL;

const char kKeyTable[kRows][kColumns] = {
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'}};

const byte kRowPins[kRows] = {KEYPAD_ROW_1_PIN, KEYPAD_ROW_2_PIN, KEYPAD_ROW_3_PIN, KEYPAD_ROW_4_PIN};
const byte kColumnPins[kColumns] = {KEYPAD_COLUMN_1_PIN, KEYPAD_COLUMN_2_PIN, KEYPAD_COLUMN_3_PIN,
                                    KEYPAD_COLUMN_4_PIN};

struct Key {
  char kchar = 0;
  KeyState kstate = IDLE;
  bool stateChanged = false;
  bool lastClosed = false;      // contact state seen in the previous scan
  unsigned long pressedAt = 0;  // when this key entered PRESSED (each key times its own hold)
};

// Scanned 4x4 matrix. A key leaves IDLE or PRESSED/HOLD only after two
// consecutive scans (20 ms apart) agree, which rejects contact bounce. Rows
// are inputs with pull-ups; one column at a time is pulled low.
class MatrixKeypad {
 public:
  MatrixKeypad() {
    for (byte r = 0; r < kRows; ++r) {
      for (byte c = 0; c < kColumns; ++c) key[r * kColumns + c].kchar = kKeyTable[r][c];
    }
  }

  // Scans at most every kScanIntervalMs. Returns true when any key changed state.
  bool getKeys() {
    const unsigned long now = millis();
    if (now - lastScanAt_ < kScanIntervalMs) return false;
    lastScanAt_ = now;

    bool closed[kKeyCount];
    scan(closed);

    bool activity = false;
    for (byte i = 0; i < kKeyCount; ++i) {
      Key& k = key[i];
      k.stateChanged = false;
      const bool stable = closed[i] == k.lastClosed;
      k.lastClosed = closed[i];
      switch (k.kstate) {
        case IDLE:
          if (closed[i] && stable) transition(k, PRESSED, now);
          break;
        case PRESSED:
          if (now - k.pressedAt > kKeyHoldTimeMs) {
            transition(k, HOLD, now);
          } else if (!closed[i] && stable) {
            transition(k, RELEASED, now);
          }
          break;
        case HOLD:
          if (!closed[i] && stable) transition(k, RELEASED, now);
          break;
        case RELEASED:
          transition(k, IDLE, now);
          break;
      }
      activity = activity || k.stateChanged;
    }
    return activity;
  }

  byte numKeys() const { return kKeyCount; }

  Key key[kKeyCount];

 private:
  static void transition(Key& k, KeyState next, unsigned long now) {
    k.kstate = next;
    k.stateChanged = true;
    if (next == PRESSED) k.pressedAt = now;
  }

  static void scan(bool (&closed)[kKeyCount]) {
    // Rows are re-initialised on every scan so the pins can be shared.
    for (byte r = 0; r < kRows; ++r) pinMode(kRowPins[r], INPUT_PULLUP);
    for (byte c = 0; c < kColumns; ++c) {
      pinMode(kColumnPins[c], OUTPUT);
      digitalWrite(kColumnPins[c], LOW);
      delayMicroseconds(5);  // let the row lines settle
      for (byte r = 0; r < kRows; ++r) closed[r * kColumns + c] = digitalRead(kRowPins[r]) == LOW;
      // High impedance ends the column pulse.
      digitalWrite(kColumnPins[c], HIGH);
      pinMode(kColumnPins[c], INPUT);
    }
  }

  unsigned long lastScanAt_ = 0;
};

bool isActiveState(KeyState state) {
  return state == PRESSED || state == HOLD;
}

bool isHeldState(KeyState state) {
  return state == HOLD;
}

int findKeyIndexForChar(MatrixKeypad& keypadRef, char keyChar) {
  for (byte i = 0; i < keypadRef.numKeys(); ++i) {
    if (keypadRef.key[i].kchar == keyChar) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

MatrixKeypad keypad;

}  // namespace

void keypadInit() {
  Serial.print("Keypad rows: ");
  for (byte i = 0; i < kRows; i++) {
    if (i > 0) {
      Serial.print(", ");
    }
    Serial.print(kRowPins[i]);
  }
  Serial.print(" | cols: ");
  for (byte i = 0; i < kColumns; i++) {
    if (i > 0) {
      Serial.print(", ");
    }
    Serial.print(kColumnPins[i]);
  }
  Serial.println();
}

bool keypadPoll(InventatoryKeyEvent& event) {
  event = {};
  if (!keypad.getKeys()) {
    return false;
  }

  const auto keyCount = keypad.numKeys();
  const int starIndex = findKeyIndexForChar(keypad, '*');
  const int dIndex = findKeyIndexForChar(keypad, 'D');
  const int hashIndex = findKeyIndexForChar(keypad, '#');
  const bool starActive = starIndex >= 0 && isActiveState(keypad.key[starIndex].kstate);
  const bool dActive = dIndex >= 0 && isActiveState(keypad.key[dIndex].kstate);
  const bool starHeld = starIndex >= 0 && isHeldState(keypad.key[starIndex].kstate);
  const bool dHeld = dIndex >= 0 && isHeldState(keypad.key[dIndex].kstate);
  const bool hashHeld = hashIndex >= 0 && isHeldState(keypad.key[hashIndex].kstate);

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
  if (holdGesture_ == HoldGesture::ProvisioningReset) {
    if (hashIndex < 0 || !isActiveState(keypad.key[hashIndex].kstate)) holdGesture_ = HoldGesture::None;
    return false;
  }

  for (byte i = 0; i < keyCount; ++i) {
    const auto& keyState = keypad.key[i];
    if (!keyState.stateChanged || keyState.kstate != PRESSED) {
      continue;
    }

    const char key = keyState.kchar;
    event.value = key;
    if (key == 'D') {
      dShortPressPending_ = true;
      continue;
    }
    if (key == '#') {
      hashShortPressPending_ = true;
      continue;
    }
    if (key >= '0' && key <= '9') {
      event.type = InventatoryKeyEventType::Digit;
      return true;
    }
    if (key == 'A') {
      event.type = InventatoryKeyEventType::Add;
      return true;
    }
    if (key == 'B') {
      event.type = InventatoryKeyEventType::Subtract;
      return true;
    }
    if (key == 'C') {
      event.type = InventatoryKeyEventType::Cancel;
      return true;
    }
  }

  if (starActive && dActive && (starHeld || dHeld)) {
    dShortPressPending_ = false;
    holdGesture_ = HoldGesture::Ota;
    event.type = InventatoryKeyEventType::OtaUpdate;
    event.value = 'D';
    return true;
  }

  if (hashHeld && !starActive && !dActive) {
    hashShortPressPending_ = false;
    holdGesture_ = HoldGesture::ProvisioningReset;
    event.type = InventatoryKeyEventType::ProvisioningReset;
    event.value = '#';
    return true;
  }

  if (dHeld && !starActive) {
    dShortPressPending_ = false;
    holdGesture_ = HoldGesture::Power;
    event.type = InventatoryKeyEventType::PowerToggle;
    event.value = 'D';
    return true;
  }

  if (dIndex >= 0 && keypad.key[dIndex].stateChanged && keypad.key[dIndex].kstate == RELEASED &&
      dShortPressPending_) {
    dShortPressPending_ = false;
    event.type = InventatoryKeyEventType::CycleCursor;
    event.value = 'D';
    return true;
  }

  if (hashIndex >= 0 && keypad.key[hashIndex].stateChanged &&
      keypad.key[hashIndex].kstate == RELEASED && hashShortPressPending_) {
    hashShortPressPending_ = false;
    event.type = InventatoryKeyEventType::DeleteDigit;
    event.value = '#';
    return true;
  }

  return false;
}
