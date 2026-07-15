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

constexpr int kProgressX = 7;
constexpr int kProgressY = 34;
constexpr int kProgressW = 114;
constexpr int kProgressH = 9;

// Exactly three bitmap fonts: warm sans body, strong sans headings, and
// monospace values/codes. All are designed for small monochrome displays.
static const uint8_t* const kFontRegular = u8g2_font_helvR08_tf;
static const uint8_t* const kFontHeading = u8g2_font_helvB08_tf;
static const uint8_t* const kFontValue = u8g2_font_profont15_tf;

}  // namespace inventatory_scan::style
