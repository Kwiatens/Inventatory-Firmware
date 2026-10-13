// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

namespace inventatory_scan {

// On/off control for the LCD backlight (see hardware::kBacklightPin). Brightness is not
// controlled, so the pin is only written when the state changes.
class Backlight {
 public:
  // Configures the pin and leaves the backlight off.
  void begin();
  void setLit(bool lit);

 private:
  bool lit_ = false;
};

}  // namespace inventatory_scan
