// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Workbench design system for the 128x64 monochrome Scan R1 LCD.

#include <U8g2lib.h>

#include <cstddef>

namespace inventatory_scan::style {

constexpr int kScreenW = 128;
constexpr int kScreenH = 64;

constexpr int kHeaderLeftX = 3;
constexpr int kHeaderRightX = 125;
constexpr int kHeaderBaselineY = 9;
constexpr int kHeaderDividerY = 12;
constexpr int kHeaderStatusGap = 5;

constexpr int kRailX = 4;
constexpr int kRailTop = 17;
constexpr int kRailBottom = 48;
constexpr int kRailWidth = 2;
constexpr int kRailHeight = kRailBottom - kRailTop + 1;
constexpr int kRailActivitySegmentH = 10;
constexpr int kRailActivityTravel = kRailHeight - kRailActivitySegmentH;
constexpr unsigned long kRailActivityStepMs = 250UL;
constexpr int kRailActivityStepsPerLeg = 6;
constexpr int kContentX = 13;
constexpr int kContentRight = 125;
constexpr int kContentW = kContentRight - kContentX;

constexpr int kTaskPrimaryY = 29;
constexpr int kTaskSecondaryY = 43;

constexpr int kQuantityItemY = 25;
constexpr int kQuantityCodeY = 34;
constexpr int kQuantityPromptY = 48;
constexpr int kQuantityValueY = 49;
constexpr int kQuantityCaretY = 50;

constexpr int kResultPrimaryY = 24;
constexpr int kResultSecondaryY = 36;
constexpr int kResultTertiaryY = 47;

constexpr int kListX = 7;
constexpr int kListFirstY = 22;
constexpr int kListRowStep = 9;
constexpr std::size_t kListMaxRows = 4;

constexpr int kFooterBaselineY = 62;
constexpr int kFooterTextX = 3;
// Give the key highlight a visible top edge and an even one-pixel horizontal
// margin around the glyph.
constexpr int kChipPadX = 1;
constexpr int kChipTopPadY = 1;
constexpr int kChipBottomPadY = 1;
constexpr int kChipRadius = 1;
constexpr int kChipLabelGap = 2;
constexpr int kChipGroupGap = 5;

// The ›i mark, as on the website and desktop app: a 5 x 7 grid of square cells.
// Rows 0-1 hold the i's dot (column 4) and the gap below it; rows 2-6 hold the
// chevron (columns 0-2) and the stem (column 4). No drop shadow: on a 1-bit LCD
// the dithered shade muddies the chevron.
constexpr int kMarkCols = 5;
constexpr int kMarkRows = 7;
constexpr int kMarkBodyRow = 2;
constexpr int kMarkBodyRows = kMarkRows - kMarkBodyRow;

// Boot: the mark on the left, its body centred like the update screen, and the
// name beside it.
constexpr int kBootMarkUnit = 6;
constexpr int kBootMarkX = 5;
constexpr int kBootColumnGap = 9;
constexpr int kBootLineGap = 4;

// Update: the mark on the left, status on the right. The mark's body, without
// the dot, is centred on the screen and the status block on that body.
constexpr int kUpdateMarkUnit = 6;
constexpr int kUpdateColumnGap = 13;
constexpr int kUpdateTitleGap = 4;
constexpr int kUpdatePercentGap = 5;
constexpr int kUpdateBarGap = 3;
constexpr int kUpdateBarBlocks = 12;
constexpr int kUpdateBarBlockW = 4;
constexpr int kUpdateBarBlockH = 5;
constexpr int kUpdateColumnW = kUpdateBarBlocks * (kUpdateBarBlockW + 1) - 1;

// Three bitmap fonts for the workflow screens: warm sans body, strong sans
// headings, and monospace values/codes. All are designed for small monochrome
// displays. The product name on the boot screen uses one larger bold face,
// ASCII-only to keep it small in flash.
static const uint8_t* const kFontRegular = u8g2_font_helvR08_tf;
static const uint8_t* const kFontHeading = u8g2_font_helvB08_tf;
static const uint8_t* const kFontValue = u8g2_font_profont15_tf;
static const uint8_t* const kFontBrand = u8g2_font_helvB10_tr;

}  // namespace inventatory_scan::style
