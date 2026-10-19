// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Layout constants for the 128x64 monochrome Scan R1 LCD.

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
constexpr int kCenterMargin = 4;

constexpr int kTaskPrimaryY = 29;
constexpr int kTaskSecondaryY = 43;

constexpr int kQuantityItemY = 24;
constexpr int kQuantityCodeY = 35;
constexpr int kQuantityPromptY = 48;
constexpr int kQuantityValueY = 49;
constexpr int kQuantityCaretY = 50;

constexpr int kTextLineStep = 9;
// Two-line wrap for long item names, in the regular face.
constexpr int kTaskWrapY = 25;
constexpr int kTaskWrapSecondaryY = 48;
constexpr int kQuantityWrapY = 23;
constexpr int kResultWrapY = 22;
constexpr int kResultWrapSecondaryY = 43;

constexpr int kResultPrimaryY = 24;
constexpr int kResultSecondaryY = 36;
constexpr int kResultTertiaryY = 47;

constexpr int kListX = 7;
constexpr int kListFirstY = 22;
constexpr int kListRowStep = 9;
constexpr std::size_t kListMaxRows = 4;

constexpr int kFooterBaselineY = 62;
constexpr int kFooterTextX = 3;
// Key chip: a visible top edge and an even one-pixel side margin around the glyph.
constexpr int kChipPadX = 1;
constexpr int kChipTopPadY = 1;
constexpr int kChipBottomPadY = 1;
constexpr int kChipRadius = 1;
constexpr int kChipLabelGap = 2;
constexpr int kChipGroupGap = 5;

// The ›i mark: a 5 x 5 cell grid, chevron in columns 0-2 and stem in column 4, no dot.
// No drop shadow: dithering muddies the chevron on a 1-bit LCD.
constexpr int kMarkCols = 5;
constexpr int kMarkBodyRows = 5;

// Boot: mark on the left, body centred like the update screen, name beside it.
constexpr int kBootMarkUnit = 6;
constexpr int kBootMarkX = 5;
constexpr int kBootColumnGap = 9;
constexpr int kBootLineGap = 4;

// Update: mark on the left, status on the right, centred on the screen body.
constexpr int kUpdateMarkUnit = 6;
constexpr int kUpdateColumnGap = 13;
constexpr int kUpdateTitleGap = 4;
constexpr int kUpdatePercentGap = 5;
constexpr int kUpdateBarGap = 3;
constexpr int kUpdateBarBlocks = 12;
constexpr int kUpdateBarBlockW = 4;
constexpr int kUpdateBarBlockH = 5;
constexpr int kUpdateColumnW = kUpdateBarBlocks * (kUpdateBarBlockW + 1) - 1;

// Top bar battery icon, above the header baseline; the charging bolt sits left of it.
constexpr int kHeaderIconY = 2;
constexpr int kBatteryBodyW = 11;
constexpr int kBatteryBodyH = 7;
constexpr int kBatteryNubW = 1;
constexpr int kBatteryNubH = 3;
constexpr int kBatteryIconGap = 3;
constexpr int kBoltW = 5;
constexpr int kBoltH = 7;
constexpr int kBoltGap = 2;

// Fault "!" mark: bar on rows 1-5, gap on row 6, dot on rows 7-8; spans the header cap height.
constexpr int kFaultMarkW = 2;
constexpr int kFaultMarkY = 1;
constexpr int kFaultMarkBarH = 5;
constexpr int kFaultMarkGapH = 1;
constexpr int kFaultMarkDotH = 2;

// Battery card: a battery at the left, the text column beside it.
constexpr int kCardBatteryX = 5;
constexpr int kCardBatteryY = 23;
constexpr int kCardBatteryW = 30;
constexpr int kCardBatteryH = 18;
constexpr int kCardNubW = 2;
constexpr int kCardNubH = 8;
constexpr int kCardTextX = 44;
constexpr int kCardTitleY = 28;
constexpr int kCardDetailY = 39;
constexpr int kCardValueY = 58;

// Low-battery warning. Title, detail and countdown line share kWarnTextX, which must be 30 or less
// so the widest countdown line ("Powering off in 10 s", 95 px) fits before kHeaderRightX.
constexpr int kWarnIconX = 7;
constexpr int kWarnIconY = 14;
constexpr int kWarnIconW = 16;
constexpr int kWarnIconH = 14;
constexpr int kWarnTextX = 29;
constexpr int kWarnTitleY = 20;
constexpr int kWarnDetailY = 31;
constexpr int kWarnBarX = 7;
constexpr int kWarnBarY = 39;
// A notice without a bar sits right under the detail.
constexpr int kWarnCountdownY = 57;
constexpr int kWarnNoticeY = 42;
constexpr int kWarnBarW = 114;
constexpr int kWarnBarH = 6;

// Soft power-off warning: centred title, countdown bar and hint; nothing else on screen.
constexpr int kPowerTitleY = 24;
constexpr int kPowerBarW = 96;
constexpr int kPowerBarX = (kScreenW - kPowerBarW) / 2;
constexpr int kPowerBarY = 32;
constexpr int kPowerBarH = 7;
constexpr int kPowerHintY = 52;
// The bar's inside, clear of its one-pixel frame and margin.
constexpr int kPowerBarFillW = kPowerBarW - 4;

// Power-off ending: the mark sinks behind the glass rule, with one line of text below it.
constexpr int kGlassEdgeY = 48;
constexpr int kGlassEdgeW = 46;
constexpr int kPowerOffMarkX = (kScreenW - kMarkCols * kBootMarkUnit) / 2;
constexpr int kPowerOffMarkY = 12;
constexpr int kPowerOffTextY = 60;

constexpr int kContrastValueY = 38;
constexpr int kContrastBarW = 70;
constexpr int kContrastBarX = (kScreenW - kContrastBarW) / 2;
constexpr int kContrastBarY = 43;
constexpr int kContrastBarH = 5;

// Confirmation dialog and pairing screens.
constexpr int kConfirmPrimaryY = 28;
constexpr int kConfirmSecondaryY = 41;
constexpr int kSetupCodeY = 31;
constexpr int kSetupPrimaryY = 43;
constexpr int kSetupSecondaryY = 52;

// Setting picker: one segment per option, the chosen one filled.
constexpr int kChoiceSegmentW = 12;
constexpr int kChoiceSegmentGap = 3;
constexpr int kChoiceLabelY = 24;
constexpr int kChoiceSegmentY = 31;
constexpr int kChoiceDetailY = 47;
constexpr int kChoiceSegmentH = 6;

// Bitmap fonts: sans body, bold sans headings, monospace values and codes. The boot brand face
// is ASCII-only to keep flash small.
static const uint8_t* const kFontRegular = u8g2_font_helvR08_tf;
static const uint8_t* const kFontHeading = u8g2_font_helvB08_tf;
static const uint8_t* const kFontValue = u8g2_font_profont15_tf;
static const uint8_t* const kFontBrand = u8g2_font_helvB10_tr;

}  // namespace inventatory_scan::style
