// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#include "display/backlight.h"

#include <Arduino.h>

#include "config/hardware.h"

namespace inventatory_scan {

void Backlight::begin() {
  pinMode(hardware::kBacklightPin, OUTPUT);
  digitalWrite(hardware::kBacklightPin, hardware::kBacklightOffLevel);
  lit_ = false;
}

void Backlight::setLit(bool lit) {
  if (lit == lit_) return;
  digitalWrite(hardware::kBacklightPin, lit ? hardware::kBacklightOnLevel : hardware::kBacklightOffLevel);
  lit_ = lit;
}

}  // namespace inventatory_scan
