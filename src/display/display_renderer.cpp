#include "display/display_renderer.h"

#include "config/hardware.h"
#include "display/display_style.h"

namespace hims_scan {

namespace {

bool isResultMode(DisplayUiMode mode) {
  return mode == DisplayUiMode::Success || mode == DisplayUiMode::Warning ||
         mode == DisplayUiMode::Error;
}

}  // namespace

DisplayRenderer::DisplayRenderer()
    : lcd_(U8G2_R0, hardware::kLcdClockPin, hardware::kLcdDataPin, hardware::kLcdCsPin,
           hardware::kLcdDcPin, hardware::kLcdResetPin) {}

void DisplayRenderer::begin() {
  lcd_.begin();
  lcd_.setContrast(hardware::kLcdContrast);
  lcd_.setPowerSave(0);
}

void DisplayRenderer::setPowerSave(bool enabled) { lcd_.setPowerSave(enabled ? 1 : 0); }

void DisplayRenderer::setContrast(uint8_t contrast) { lcd_.setContrast(constrain(contrast, 0, 63)); }

void DisplayRenderer::normalizeStartLine() {
  for (uint8_t attempt = 0; attempt < hardware::kLcdStartLineRecoveryAttempts; ++attempt) {
    // Separate transactions make one corrupted command unlikely to leave the panel shifted.
    lcd_.sendF("c", 0x40);  // ST7565: display start line = 0.
  }
}

void DisplayRenderer::drawClipped(int x, int y, int maxWidth, const String& text) {
  String clipped = text;
  while (clipped.length() > 0 && lcd_.getStrWidth(clipped.c_str()) > maxWidth) clipped.remove(clipped.length() - 1);
  if (clipped.length() < text.length() && clipped.length() > 3) {
    clipped.remove(clipped.length() - 3);
    clipped += "...";
  }
  lcd_.drawStr(x, y, clipped.c_str());
}

// ---------------------------------------------------------------------------
// Small hand-drawn glyphs (1-bit, no icon-font tables).
// ---------------------------------------------------------------------------

void DisplayRenderer::drawWifi(int x, int top, bool online, bool connecting, const RenderAnim& anim) {
  const int base = top + 8;
  const int litBars = connecting ? (anim.spinnerFrame % 4) : 3;
  for (int i = 0; i < 4; ++i) {
    const int bx = x + i * 2;
    const int h = 2 + i * 2;
    const bool lit = online ? true : (connecting ? (i <= litBars) : false);
    if (lit) {
      lcd_.drawVLine(bx, base - h, h);
    } else {
      lcd_.drawPixel(bx, base - 1);
    }
  }
  if (!online && !connecting) lcd_.drawLine(x - 1, top, x + 7, base);  // offline slash
}

void DisplayRenderer::drawMonitor(int x, int top, bool filled) {
  lcd_.drawFrame(x, top, 7, 5);
  if (filled) lcd_.drawBox(x + 1, top + 1, 5, 3);
  lcd_.drawHLine(x + 1, top + 6, 5);  // stand base
  lcd_.drawVLine(x + 3, top + 5, 1);
}

void DisplayRenderer::drawSpinner(int cx, int cy, uint8_t frame) {
  static const int8_t dx[8] = {3, 2, 0, -2, -3, -2, 0, 2};
  static const int8_t dy[8] = {0, 2, 3, 2, 0, -2, -3, -2};
  for (int i = 0; i < 8; ++i) {
    const int px = cx + dx[i];
    const int py = cy + dy[i];
    if (i == frame % 8) {
      lcd_.drawDisc(px, py, 1);
    } else {
      lcd_.drawPixel(px, py);
    }
  }
}

void DisplayRenderer::drawCheck(int cx, int cy, int r, uint8_t progress) {
  lcd_.drawCircle(cx, cy, r);
  const int ax = cx - 6, ay = cy;
  const int bx = cx - 2, by = cy + 4;
  const int gx = cx + 6, gy = cy - 6;
  const int p = constrain(progress, 0, 100);
  const int f1 = min(100, p * 100 / 40);  // first stroke over 0..40%
  const int p1x = ax + (bx - ax) * f1 / 100;
  const int p1y = ay + (by - ay) * f1 / 100;
  lcd_.drawLine(ax, ay, p1x, p1y);
  if (p > 40) {
    const int f2 = (p - 40) * 100 / 60;  // second stroke over 40..100%
    const int p2x = bx + (gx - bx) * f2 / 100;
    const int p2y = by + (gy - by) * f2 / 100;
    lcd_.drawLine(bx, by, p2x, p2y);
  }
}

void DisplayRenderer::drawCross(int cx, int cy, int r) {
  lcd_.drawCircle(cx, cy, r);
  lcd_.drawLine(cx - 5, cy - 5, cx + 5, cy + 5);
  lcd_.drawLine(cx - 5, cy + 5, cx + 5, cy - 5);
}

void DisplayRenderer::drawWarningBang(int cx, int cy, int r) {
  lcd_.drawLine(cx, cy - r, cx - r, cy + r);
  lcd_.drawLine(cx, cy - r, cx + r, cy + r);
  lcd_.drawLine(cx - r, cy + r, cx + r, cy + r);
  lcd_.drawVLine(cx, cy - 3, 5);
  lcd_.drawPixel(cx, cy + r - 3);
}

// ---------------------------------------------------------------------------
// Header
// ---------------------------------------------------------------------------

void DisplayRenderer::drawStatusIcons(const DisplayModel& model, const RenderAnim& anim) {
  if (model.mode == DisplayUiMode::Ota || model.mode == DisplayUiMode::Setup) return;
  lcd_.setDrawColor(0);
  int right = style::kScreenW - style::kHeaderRightPad;

  // Queue badge (rightmost): count of pending events.
  if (model.queueDepth > 0) {
    lcd_.setFont(style::kFontSmall);
    const String q(static_cast<unsigned>(model.queueDepth));
    const int w = lcd_.getStrWidth(q.c_str());
    lcd_.drawStr(right - w, style::kHeaderBaselineY, q.c_str());
    lcd_.drawFrame(right - w - 2, 1, w + 3, style::kHeaderH - 2);
    right -= (w + 5);
  }

  // PC link (monitor): solid when linked, outline while searching.
  const bool pcLinked = model.pcOnline || model.connectivity == DisplayConnectivityState::Syncing;
  if (pcLinked) {
    right -= style::kMonitorW;
    drawMonitor(right, style::kIconTop, true);
    right -= 2;
  } else if (model.wifiOnline) {
    right -= style::kMonitorW;
    drawMonitor(right, style::kIconTop, false);
    right -= 2;
  }

  // Wi-Fi signal bars.
  const bool connecting = model.connectivity == DisplayConnectivityState::WifiConnecting;
  right -= style::kWifiW;
  drawWifi(right, style::kIconTop, model.wifiOnline, connecting, anim);
  right -= 3;

  // In-progress spinner for genuinely transient connectivity work only. A
  // persistent "searching" state stays static (the outline monitor conveys it),
  // matching DisplayController::spinnerActive() so the spinner never freezes.
  const bool busy = connecting || model.connectivity == DisplayConnectivityState::Syncing;
  if (busy) {
    right -= (2 * style::kSpinnerR + 1);
    drawSpinner(right + style::kSpinnerR, style::kHeaderH / 2, anim.spinnerFrame);
  }

  lcd_.setDrawColor(1);
}

void DisplayRenderer::drawHeader(const DisplayModel& model, const RenderAnim& anim) {
  lcd_.setDrawColor(1);
  lcd_.drawBox(0, 0, style::kScreenW, style::kHeaderH);
  lcd_.setDrawColor(0);
  lcd_.setFont(style::kFontSmall);
  String status;
  if (model.mode == DisplayUiMode::Ota) {
    status = "OTA UPDATE";
  } else if (model.mode == DisplayUiMode::Setup) {
    status = "BLE SETUP";
  } else {
    switch (model.connectivity) {
      case DisplayConnectivityState::WifiConnecting: status = "WIFI"; break;
      case DisplayConnectivityState::PcSearching: status = "LINK PC"; break;
      case DisplayConnectivityState::Syncing: status = "SYNC"; break;
      case DisplayConnectivityState::Online: status = "ONLINE"; break;
      case DisplayConnectivityState::Offline: status = "OFFLINE"; break;
    }
  }
  // Leave room on the right for the status-icon cluster (~44px).
  drawClipped(style::kHeaderTextX, style::kHeaderBaselineY, style::kScreenW - 46, status);
  lcd_.setDrawColor(1);
  drawStatusIcons(model, anim);
}

// ---------------------------------------------------------------------------
// Splash (procedural brand logo from the favicon motif) + boot
// ---------------------------------------------------------------------------

void DisplayRenderer::drawSplash(const DisplayModel& model) {
  lcd_.setDrawColor(1);
  // Logo: rounded-square frame holding "list rows" with a highlighted scan bar.
  constexpr int lw = 40, lh = 26;
  const int lx = (style::kScreenW - lw) / 2;
  constexpr int ly = 2;
  lcd_.drawRFrame(lx, ly, lw, lh, 4);
  lcd_.drawHLine(lx + 8, ly + 6, lw - 16);   // top list row
  lcd_.drawBox(lx + 4, ly + 11, lw - 8, 4);  // highlighted scan bar (middle)
  lcd_.drawHLine(lx + 8, ly + 20, lw - 22);  // bottom list row (shorter)

  lcd_.setFont(style::kFontWordmark);
  const int titleX = (style::kScreenW - lcd_.getStrWidth(model.title.c_str())) / 2;
  lcd_.drawStr(max(0, titleX), 46, model.title.c_str());
  lcd_.setFont(style::kFontBody);
  const int subtitleX = (style::kScreenW - lcd_.getStrWidth(model.line1.c_str())) / 2;
  lcd_.drawStr(max(0, subtitleX), 60, model.line1.c_str());
}

// ---------------------------------------------------------------------------
// Result screens (glyph + text)
// ---------------------------------------------------------------------------

void DisplayRenderer::drawResult(const DisplayModel& model, const RenderAnim& anim) {
  lcd_.setDrawColor(1);
  const int cx = style::kResultGlyphCx, cy = style::kResultGlyphCy, r = style::kResultGlyphR;
  switch (model.mode) {
    case DisplayUiMode::Success: drawCheck(cx, cy, r, anim.successProgress); break;
    case DisplayUiMode::Warning: drawWarningBang(cx, cy, r); break;
    default: drawCross(cx, cy, r); break;  // Error
  }
  const int w = style::kScreenW - style::kResultTextX - 2;
  lcd_.setFont(style::kFontBody);
  drawClipped(style::kResultTextX, style::kResultTitleY, w, model.title);
  lcd_.setFont(style::kFontSmall);
  drawClipped(style::kResultTextX, style::kResultLine1Y, w, model.line1);
  drawClipped(style::kResultTextX, style::kResultLine2Y, w, model.line2);
}

// ---------------------------------------------------------------------------
// Body
// ---------------------------------------------------------------------------

void DisplayRenderer::drawArrow(const RenderAnim& anim) {
  if (anim.pulseActive) {
    lcd_.drawTriangle(style::kArrowTipX, style::kArrowMidY, style::kArrowBackX, style::kArrowTopY,
                      style::kArrowBackX, style::kArrowBotY);
    // Dark scan line sweeping down the filled chevron.
    const int span = style::kArrowBotY - style::kArrowTopY;
    const int y = style::kArrowTopY + (anim.pulsePhase * span) / (style::kPulsePhases - 1);
    lcd_.setDrawColor(0);
    lcd_.drawHLine(0, y, style::kArrowBackX + 1);
    lcd_.drawHLine(0, y + 1, style::kArrowBackX + 1);
    lcd_.setDrawColor(1);
  } else {
    lcd_.drawLine(style::kArrowTipX, style::kArrowMidY, style::kArrowBackX - 1, style::kArrowTopY);
    lcd_.drawLine(style::kArrowTipX, style::kArrowMidY, style::kArrowBackX - 1, style::kArrowBotY);
    lcd_.drawVLine(style::kArrowBackX - 1, style::kArrowTopY, style::kArrowBotY - style::kArrowTopY);
  }
}

void DisplayRenderer::drawBody(const DisplayModel& model, const RenderAnim& anim) {
  constexpr int x = style::kBodyX;
  constexpr int width = style::kBodyW;
  if (model.mode == DisplayUiMode::Menu || model.mode == DisplayUiMode::QuickLabels ||
      model.mode == DisplayUiMode::QuickSettings) {
    // Menus intentionally use the entire panel: no connectivity bar, scan arrow, or footer competes with controls.
    lcd_.setFont(style::kFontBody);
    drawClipped(style::kMenuTitleX, style::kMenuTitleY, style::kScreenW - 2 * style::kMenuTitleX, model.title);
    lcd_.drawHLine(0, style::kMenuDividerY, style::kScreenW);
    for (std::size_t index = 0; index < model.menuLineCount && index < style::kMenuMaxRows; ++index) {
      drawClipped(style::kMenuRowX, style::kMenuRowY0 + static_cast<int>(index) * style::kMenuRowStep,
                  style::kScreenW - 2 * style::kMenuRowX, model.menuLines[index]);
    }
    return;
  }
  if (isResultMode(model.mode)) {
    drawResult(model, anim);
    return;
  }
  if (model.mode == DisplayUiMode::Quantity) {
    lcd_.setFont(style::kFontSmall);
    drawClipped(x, style::kQtyTitleY, width, model.title);
    drawClipped(x, style::kQtyItemY, width, model.line1);
    lcd_.setFont(style::kFontNumeral);
    lcd_.setCursor(x, style::kQtyNumeralY);
    lcd_.print(model.quantityText);
    const auto prefix = model.quantityText.substring(0, min(model.quantityCursor, model.quantityText.length()));
    const int cursorX = x + lcd_.getStrWidth(prefix.c_str());
    int cursorWidth = style::kQtyCaretMinW;
    if (model.quantityCursor < model.quantityText.length()) {
      const auto digit = model.quantityText.substring(model.quantityCursor, model.quantityCursor + 1);
      cursorWidth = max(6, static_cast<int>(lcd_.getStrWidth(digit.c_str())));
    }
    lcd_.drawHLine(cursorX, style::kQtyUnderlineY, cursorWidth);
    return;
  }
  if (model.mode == DisplayUiMode::Ota) {
    lcd_.setFont(style::kFontBody);
    drawClipped(x, style::kOtaTitleY, width, model.title);
    lcd_.drawFrame(x, style::kOtaBarY, style::kOtaBarW, style::kOtaBarH);
    lcd_.drawBox(x + 2, style::kOtaBarY + 2,
                 ((style::kOtaBarW - 4) * constrain(model.otaPercent, 0, 100)) / 100, style::kOtaBarH - 4);
    return;
  }
  lcd_.setFont(style::kFontBody);
  drawClipped(x, style::kBodyTitleY, width, model.title);
  lcd_.setFont(style::kFontSmall);
  drawClipped(x, style::kBodyLine1Y, width, model.line1);
  drawClipped(x, style::kBodyLine2Y, width, model.line2);
  // Live spinner for indeterminate waits (printing, BLE connecting).
  if (model.mode == DisplayUiMode::Printing || model.mode == DisplayUiMode::Setup) {
    drawSpinner(style::kScreenW - 12, style::kBodyLine1Y - 3, anim.spinnerFrame);
  }
}

// ---------------------------------------------------------------------------
// Footer (key-hint chips)
// ---------------------------------------------------------------------------

bool DisplayRenderer::isKeyToken(const String& word) {
  if (word.length() == 0) return false;
  const char key = word[0];
  if (key < 'A' || key > 'D') return false;
  if (word.length() == 1) return true;
  // "A+", "B-", "D:" -> key letter followed by a non-alphanumeric operator/marker.
  return !isalnum(static_cast<unsigned char>(word[1]));
}

void DisplayRenderer::drawKeyChip(int& x, int baselineY, char key) {
  const String glyph(key);
  const int glyphW = lcd_.getStrWidth(glyph.c_str());
  // Size the box from the live font ascent so padding above/below the glyph ink
  // is symmetric (ink occupies rows [baselineY - ascent, baselineY - 1]).
  const int ascent = lcd_.getAscent();
  const int boxW = glyphW + 2 * style::kChipPadX;
  const int boxTop = baselineY - ascent - style::kChipPadY;
  const int boxH = ascent + 2 * style::kChipPadY;
  lcd_.setDrawColor(1);
  lcd_.drawRBox(x, boxTop, boxW, boxH, style::kChipRadius);
  lcd_.setDrawColor(0);
  lcd_.drawStr(x + style::kChipPadX, baselineY, glyph.c_str());
  lcd_.setDrawColor(1);
  x += boxW;
}

void DisplayRenderer::drawFooter(const String& footer) {
  lcd_.drawHLine(0, style::kFooterDividerY, style::kScreenW);
  lcd_.setFont(style::kFontSmall);
  const int y = style::kFooterBaselineY;
  const int spaceW = lcd_.getStrWidth(" ");
  int x = style::kFooterTextX;
  int start = 0;
  const int len = static_cast<int>(footer.length());
  while (start <= len && x < style::kScreenW) {
    int end = footer.indexOf(' ', start);
    if (end < 0) end = len;
    const String word = footer.substring(start, end);
    start = end + 1;
    if (word.length() == 0) continue;  // collapse runs of spaces
    if (isKeyToken(word)) {
      drawKeyChip(x, y, word[0]);
      if (word.length() > 1) {
        const String rest = word.substring(1);
        x += style::kChipLabelGap;
        lcd_.drawStr(x, y, rest.c_str());
        x += lcd_.getStrWidth(rest.c_str());
      }
      x += style::kChipGroupGap;
    } else {
      lcd_.drawStr(x, y, word.c_str());
      x += lcd_.getStrWidth(word.c_str()) + spaceW;
    }
  }
}

// ---------------------------------------------------------------------------
// Frame assembly
// ---------------------------------------------------------------------------

void DisplayRenderer::render(const DisplayModel& model, const RenderAnim& anim) {
  lcd_.clearBuffer();
  if (model.mode == DisplayUiMode::Splash) {
    drawSplash(model);
    lcd_.sendBuffer();
    normalizeStartLine();
    return;
  }
  const bool fullScreenMenu = model.mode == DisplayUiMode::Menu || model.mode == DisplayUiMode::QuickLabels ||
                              model.mode == DisplayUiMode::QuickSettings;
  if (!fullScreenMenu) drawHeader(model, anim);
  // The scan arrow marks "ready to scan"; suppress it where a body glyph owns
  // the gutter (result screens) or the panel is asleep.
  if (!fullScreenMenu && model.mode != DisplayUiMode::Standby && !isResultMode(model.mode)) {
    drawArrow(anim);
  }
  drawBody(model, anim);
  if (!fullScreenMenu) drawFooter(model.footer);
  lcd_.sendBuffer();
  normalizeStartLine();
}

}  // namespace hims_scan
