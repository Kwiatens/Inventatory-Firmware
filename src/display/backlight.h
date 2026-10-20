// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

namespace inventatory_scan {

// On/off only, no brightness control. The pin is written only when the state changes.
class Backlight {
 public:
  // Configures the pin and leaves the backlight off.
  void begin();
  void setLit(bool lit);
  bool lit() const { return lit_; }

 private:
  bool lit_ = false;
};

}  // namespace inventatory_scan
