// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#pragma once
typedef int gpio_num_t;
enum { RTC_GPIO_MODE_OUTPUT_ONLY, RTC_GPIO_MODE_INPUT_ONLY };
inline void rtc_gpio_hold_dis(int) {} inline void rtc_gpio_hold_en(int) {} inline void rtc_gpio_deinit(int) {} inline void rtc_gpio_init(int) {}
inline void rtc_gpio_set_direction(int, int) {} inline void rtc_gpio_set_level(int, unsigned) {} inline void rtc_gpio_pulldown_dis(int) {} inline void rtc_gpio_pullup_en(int) {}
inline unsigned rtc_gpio_get_level(int) { return 1; }
