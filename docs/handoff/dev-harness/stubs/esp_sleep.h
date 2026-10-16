// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#pragma once
enum esp_sleep_wakeup_cause_t { ESP_SLEEP_WAKEUP_EXT1 = 2 };
inline void esp_deep_sleep_start() {}
inline esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause() { return ESP_SLEEP_WAKEUP_EXT1; }
enum { ESP_EXT1_WAKEUP_ANY_LOW = 0 };
inline void esp_sleep_enable_ext1_wakeup(unsigned long long, int) {}
