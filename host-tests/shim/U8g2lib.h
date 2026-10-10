// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Host stand-in for U8g2lib.h. display_style.h only takes the addresses of the
// font bitmaps; the host tests never draw, so placeholder arrays are enough.
#pragma once

#include <cstdint>

inline constexpr std::uint8_t u8g2_font_helvR08_tf[1] = {0};
inline constexpr std::uint8_t u8g2_font_helvB08_tf[1] = {0};
inline constexpr std::uint8_t u8g2_font_profont15_tf[1] = {0};
inline constexpr std::uint8_t u8g2_font_helvB10_tr[1] = {0};
