// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#pragma once
#include <cstddef>
#include <cstdint>
#define U_FLASH 0
struct UpdateClass {
  bool begin(size_t, int = U_FLASH) { return true; } size_t write(uint8_t*, size_t len) { return len; }
  bool end(bool = false) { return true; } void abort() {} uint8_t getError() { return 0; }
};
inline UpdateClass Update;
