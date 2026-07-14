#pragma once

// Single source of truth for the Scan R1 display look: layout geometry, the
// monospace "terminal" font scale, and key-hint chip metrics. The renderer
// references these names instead of inlining magic numbers so the whole panel
// can be retuned from one place. Panel is 128x64, 1-bit (colour is N/A).

#include <U8g2lib.h>

#include <cstddef>

namespace hims_scan {
namespace style {

// --- Panel ------------------------------------------------------------------
constexpr int kScreenW = 128;
constexpr int kScreenH = 64;

// --- Header (inverted status bar) -------------------------------------------
constexpr int kHeaderH = 11;          // filled box height, rows 0..10
constexpr int kHeaderTextX = 3;
constexpr int kHeaderBaselineY = 8;
constexpr int kHeaderRightPad = 2;    // gap from the right edge for icons

// --- Body -------------------------------------------------------------------
constexpr int kBodyX = 15;            // left gutter holds the scan arrow
constexpr int kBodyW = 111;
constexpr int kBodyTitleY = 23;
constexpr int kBodyLine1Y = 35;
constexpr int kBodyLine2Y = 46;

// Quantity screen (large numeral + edit caret).
constexpr int kQtyTitleY = 18;
constexpr int kQtyItemY = 27;
constexpr int kQtyNumeralY = 49;      // numeral baseline
constexpr int kQtyUnderlineY = 51;
constexpr int kQtyCaretMinW = 8;

// OTA / progress bar.
constexpr int kOtaTitleY = 25;
constexpr int kOtaBarY = 32;
constexpr int kOtaBarW = 106;
constexpr int kOtaBarH = 10;

// Result-screen glyph (Success/Warning/Error): drawn in the left body area.
constexpr int kResultGlyphCx = 15;    // glyph centre X (uses the scan gutter)
constexpr int kResultGlyphCy = 32;    // glyph centre Y
constexpr int kResultGlyphR = 12;     // glyph radius
constexpr int kResultTextX = 34;      // text block starts right of the glyph
constexpr int kResultTitleY = 21;
constexpr int kResultLine1Y = 34;
constexpr int kResultLine2Y = 45;

// --- Footer (key-hint strip) ------------------------------------------------
constexpr int kFooterDividerY = 52;
constexpr int kFooterBaselineY = 61;
constexpr int kFooterTextX = 3;
constexpr int kFooterW = kScreenW - 2 * kFooterTextX;

// --- Menu (full-screen list) ------------------------------------------------
constexpr int kMenuTitleX = 4;
constexpr int kMenuTitleY = 9;
constexpr int kMenuDividerY = 12;
constexpr int kMenuRowX = 7;
constexpr int kMenuRowY0 = 24;
constexpr int kMenuRowStep = 10;
constexpr std::size_t kMenuMaxRows = 5;

// --- Key-hint chips ("[A] OK") ----------------------------------------------
// Chip = inverted rounded box around the key glyph, then a plain label. The box
// is sized from the live font ascent so vertical padding is symmetric.
constexpr int kChipPadX = 2;          // horizontal padding inside a chip
constexpr int kChipPadY = 1;          // vertical padding above/below the glyph ink
constexpr int kChipRadius = 1;
constexpr int kChipLabelGap = 2;      // chip box -> its label
constexpr int kChipGroupGap = 6;      // one [key]+label group -> the next

// --- Scan arrow / left gutter ----------------------------------------------
constexpr int kArrowTipX = 1;
constexpr int kArrowMidY = 32;
constexpr int kArrowBackX = 11;
constexpr int kArrowTopY = 23;
constexpr int kArrowBotY = 41;

// --- Motion --------------------------------------------------------------
constexpr uint8_t kPulsePhases = 6;     // scan-sweep steps down the gutter
constexpr int kPulseWindowMs = 350;     // matches DisplayController::pulseScan()
constexpr int kSpinnerPeriodMs = 220;   // per rotating-tick frame (calm cadence)
constexpr int kSuccessDrawMs = 300;     // checkmark draw-in duration
constexpr int kSpinnerR = 3;            // spinner ring radius

// --- Header status icons (drawn in the inverted bar, colour 0) ---------------
constexpr int kIconTop = 2;
constexpr int kWifiW = 8;
constexpr int kMonitorW = 9;

// --- Fonts (monospace terminal family, mirrors the desktop app identity) ----
// profont is a fixed-width bitmap terminal face; _tf = full glyphs, _tn = digits.
static const uint8_t* const kFontSmall = u8g2_font_profont10_tf;     // header/footer/secondary
static const uint8_t* const kFontBody = u8g2_font_profont11_tf;      // titles, menu rows, body
static const uint8_t* const kFontWordmark = u8g2_font_profont17_tf;  // splash wordmark
static const uint8_t* const kFontNumeral = u8g2_font_profont22_tn;   // large quantity digits

}  // namespace style
}  // namespace hims_scan
