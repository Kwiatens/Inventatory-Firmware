// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#pragma once
#include <cstdint>
inline uint32_t esp_random() { return 4; }
inline int esp_reset_reason() { return 0; }
enum { ESP_RST_DEEPSLEEP = 8 };
// From esp_attr.h on the device: a variable kept in RTC memory through deep sleep.
#ifndef RTC_DATA_ATTR
#define RTC_DATA_ATTR
#endif
