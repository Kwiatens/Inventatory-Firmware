// Scan R1 LCD rendering routines.
#include "display/display_renderer.h"

#include <algorithm>
#include <cctype>

#include "HimsScanUiPolicy.h"
#include "config/hardware.h"
#include "display/display_style.h"

namespace hims_scan {

DisplayRenderer::DisplayRenderer()
    : lcd_(U8G2_R0, hardware::kLcdClockPin, hardware::kLcdDataPin,
           hardware::kLcdCsPin, hardware::kLcdDcPin, hardware::kLcdResetPin) {}

void DisplayRenderer::begin() {
  lcd_.begin();
  lcd_.setContrast(hardware::kLcdContrast);
  lcd_.setFontMode(1);
  lcd_.setDrawColor(1);
}

void DisplayRenderer::setPowerSave(bool enabled) { lcd_.setPowerSave(enabled ? 1 : 0); }

void DisplayRenderer::setContrast(uint8_t contrast) { lcd_.setContrast(constrain(contrast, 0, 63)); }

void DisplayRenderer::normalizeStartLine() {
  for (uint8_t attempt = 0; attempt < hardware::kLcdStartLineRecoveryAttempts; ++attempt) {
    lcd_.sendF("c", 0x40);
  }
}

void DisplayRenderer::drawClipped(int x, int y, int maxWidth, const String& value) {
  if (maxWidth <= 0 || value.length() == 0) return;
  const int ellipsisWidth = lcd_.getStrWidth("...");
  const std::size_t prefixLength = displayClippedPrefixLength(
      value.length(), maxWidth, ellipsisWidth, [this, &value](std::size_t length) {
        return lcd_.getStrWidth(value.substring(0, length).c_str());
      });
  if (prefixLength == value.length()) {
    lcd_.drawStr(x, y, value.c_str());
    return;
  }
  const String clipped = value.substring(0, prefixLength) + "...";
  if (lcd_.getStrWidth(clipped.c_str()) <= maxWidth) lcd_.drawStr(x, y, clipped.c_str());
}

void DisplayRenderer::drawWrapped(int x, int firstBaselineY, int maxWidth, const String& value,
                                  std::size_t maxLines, int lineStep) {
  int cursor = 0;
  const int length = static_cast<int>(value.length());
  for (std::size_t lineIndex = 0; lineIndex < maxLines && cursor < length; ++lineIndex) {
    while (cursor < length && value[cursor] == ' ') ++cursor;
    String line;
    int next = cursor;
    while (next < length) {
      int wordEnd = value.indexOf(' ', next);
      if (wordEnd < 0) wordEnd = length;
      const String word = value.substring(next, wordEnd);
      const String candidate = line.length() > 0 ? line + " " + word : word;
      if (line.length() > 0 && lcd_.getStrWidth(candidate.c_str()) > maxWidth) break;
      line = candidate;
      next = wordEnd;
      while (next < length && value[next] == ' ') ++next;
      if (lcd_.getStrWidth(line.c_str()) > maxWidth) break;
    }
    cursor = next;
    if (lineIndex + 1U == maxLines && cursor < length) line += " " + value.substring(cursor);
    drawClipped(x, firstBaselineY + static_cast<int>(lineIndex) * lineStep, maxWidth, line);
  }
}

String DisplayRenderer::pcStatusText(const DisplayModel& model) const {
  switch (model.connectivity) {
    case DisplayConnectivityState::Online: return "PC linked";
    case DisplayConnectivityState::Syncing: return "PC syncing";
    default: return "PC offline";
  }
}

void DisplayRenderer::drawHeader(const DisplayModel& model, const RenderActivity& activity) {
  const String header = model.animateEllipsis
                            ? model.header + (activity.alternate ? "..." : ".")
                            : model.header;
  lcd_.setFont(style::kFontHeading);
  const String rightText = model.pageText.length() > 0
                               ? model.pageText
                               : (model.showPcStatus ? pcStatusText(model) : String());
  const int titleWidth = lcd_.getStrWidth(header.c_str());
  bool showRight = rightText.length() > 0;
  int rightWidth = 0;
  if (showRight) {
    lcd_.setFont(model.pageText.length() > 0 ? style::kFontValue : style::kFontRegular);
    rightWidth = lcd_.getStrWidth(rightText.c_str());
    showRight = displayHeaderCanShowStatus(
        titleWidth, rightWidth, style::kHeaderRightX - style::kHeaderLeftX,
        style::kHeaderStatusGap);
  }

  lcd_.setFont(style::kFontHeading);
  drawClipped(style::kHeaderLeftX, style::kHeaderBaselineY,
              showRight ? style::kHeaderRightX - style::kHeaderLeftX - rightWidth -
                              style::kHeaderStatusGap
                        : style::kHeaderRightX - style::kHeaderLeftX,
              header);
  if (showRight) {
    lcd_.setFont(model.pageText.length() > 0 ? style::kFontValue : style::kFontRegular);
    lcd_.drawStr(style::kHeaderRightX - rightWidth, style::kHeaderBaselineY, rightText.c_str());
  }
  lcd_.drawHLine(style::kHeaderLeftX, style::kHeaderDividerY,
                 style::kHeaderRightX - style::kHeaderLeftX);
}

void DisplayRenderer::drawRail(DisplayRailStyle railStyle, const RenderActivity& activity) {
  const int height = style::kRailBottom - style::kRailTop + 1;
  switch (railStyle) {
    case DisplayRailStyle::Thin:
      lcd_.drawVLine(style::kRailX + 1, style::kRailTop, height);
      break;
    case DisplayRailStyle::Solid:
      lcd_.drawBox(style::kRailX, style::kRailTop, 4, height);
      break;
    case DisplayRailStyle::Warning:
      for (int y = style::kRailTop; y <= style::kRailBottom; y += 4) {
        lcd_.drawBox(style::kRailX, y, 3, 2);
      }
      break;
    case DisplayRailStyle::Error:
      lcd_.setFont(style::kFontValue);
      lcd_.drawStr(style::kRailX - 1, 38, "!");
      break;
    case DisplayRailStyle::Activity:
      if (activity.alternate) {
        lcd_.drawBox(style::kRailX, style::kRailTop, 2, 14);
        lcd_.drawBox(style::kRailX, style::kRailBottom - 8, 2, 9);
      } else {
        lcd_.drawBox(style::kRailX, style::kRailTop, 2, 9);
        lcd_.drawBox(style::kRailX, style::kRailBottom - 13, 2, 14);
      }
      break;
    default:
      break;
  }
}

void DisplayRenderer::drawTask(const DisplayModel& model, const RenderActivity& activity) {
  (void)activity;
  const String& primary = model.primary;
  lcd_.setFont(style::kFontHeading);
  const bool primaryFits = lcd_.getStrWidth(primary.c_str()) <= style::kContentW;
  if (primaryFits) {
    drawClipped(style::kContentX, style::kTaskPrimaryY, style::kContentW, primary);
  } else {
    lcd_.setFont(style::kFontRegular);
    drawWrapped(style::kContentX, 25, style::kContentW, primary, 2, 9);
  }
  lcd_.setFont(style::kFontRegular);
  drawWrapped(style::kContentX, primaryFits ? style::kTaskSecondaryY : 48, style::kContentW,
              model.secondary, 1, 9);
}

void DisplayRenderer::drawQuantity(const DisplayModel& model, const RenderActivity& activity) {
  lcd_.setFont(style::kFontHeading);
  const bool itemFits = lcd_.getStrWidth(model.primary.c_str()) <= style::kContentW;
  if (itemFits) {
    drawClipped(style::kContentX, style::kQuantityItemY, style::kContentW, model.primary);
    lcd_.setFont(style::kFontRegular);
    drawClipped(style::kContentX, style::kQuantityCodeY, style::kContentW, model.secondary);
  } else {
    lcd_.setFont(style::kFontRegular);
    drawWrapped(style::kContentX, 23, style::kContentW, model.primary, 2, 9);
  }

  lcd_.setFont(style::kFontRegular);
  drawClipped(style::kContentX, style::kQuantityPromptY, style::kContentW,
              model.quantityPrompt);
  const int promptWidth = lcd_.getStrWidth(model.quantityPrompt.c_str());
  lcd_.setFont(style::kFontValue);
  const int valueX = style::kContentX + promptWidth + 3;
  lcd_.drawStr(valueX, style::kQuantityValueY, model.quantityText.c_str());
  if (activity.quantityCursorVisible) {
    const auto prefix = model.quantityText.substring(0, min(model.quantityCursor, model.quantityText.length()));
    const int caretX = valueX + lcd_.getStrWidth(prefix.c_str());
    int caretWidth = 7;
    if (model.quantityCursor < model.quantityText.length()) {
      const auto digit = model.quantityText.substring(model.quantityCursor, model.quantityCursor + 1);
      caretWidth = max(5, static_cast<int>(lcd_.getStrWidth(digit.c_str())));
    }
    lcd_.drawHLine(caretX, style::kQuantityCaretY, caretWidth);
  }
}

void DisplayRenderer::drawResult(const DisplayModel& model) {
  lcd_.setFont(style::kFontHeading);
  const bool primaryFits = lcd_.getStrWidth(model.primary.c_str()) <= style::kContentW;
  if (primaryFits) {
    drawClipped(style::kContentX, style::kResultPrimaryY, style::kContentW, model.primary);
  } else {
    lcd_.setFont(style::kFontRegular);
    drawWrapped(style::kContentX, 22, style::kContentW, model.primary, 2, 9);
  }
  lcd_.setFont(style::kFontRegular);
  drawClipped(style::kContentX, primaryFits ? style::kResultSecondaryY : 43,
              style::kContentW, model.secondary);
  if (primaryFits) {
    lcd_.setFont(style::kFontValue);
    drawClipped(style::kContentX, style::kResultTertiaryY, style::kContentW, model.tertiary);
  }
}

void DisplayRenderer::drawList(const DisplayModel& model) {
  lcd_.setFont(style::kFontRegular);
  for (std::size_t index = 0; index < model.listRowCount && index < style::kListMaxRows; ++index) {
    drawClipped(style::kListX,
                style::kListFirstY + static_cast<int>(index) * style::kListRowStep,
                style::kScreenW - 2 * style::kListX, model.listRows[index]);
  }
}

void DisplayRenderer::drawContrast(const DisplayModel& model) {
  lcd_.setFont(style::kFontValue);
  const int width = lcd_.getStrWidth(model.primary.c_str());
  lcd_.drawStr((style::kScreenW - width) / 2, 38, model.primary.c_str());
  lcd_.drawFrame(29, 43, 70, 5);
  const int level = constrain(model.primary.toInt(), 0, 63);
  lcd_.drawBox(31, 45, (66 * level) / 63, 1);
}

void DisplayRenderer::drawConfirm(const DisplayModel& model) {
  lcd_.setFont(style::kFontHeading);
  drawClipped(style::kListX, 28, style::kScreenW - 2 * style::kListX, model.primary);
  lcd_.setFont(style::kFontRegular);
  drawWrapped(style::kListX, 41, style::kScreenW - 2 * style::kListX,
              model.secondary, 1, 9);
}

void DisplayRenderer::drawSetup(const DisplayModel& model) {
  lcd_.setFont(style::kFontValue);
  char codeBuffer[7];
  snprintf(codeBuffer, sizeof(codeBuffer), "%06lu", static_cast<unsigned long>(model.setupCode));
  const String code(codeBuffer);
  const int width = lcd_.getStrWidth(code.c_str());
  lcd_.drawStr((style::kScreenW - width) / 2, 31, code.c_str());
  lcd_.setFont(style::kFontHeading);
  const int primaryWidth = lcd_.getStrWidth(model.primary.c_str());
  if (primaryWidth <= style::kScreenW - 8) {
    lcd_.drawStr((style::kScreenW - primaryWidth) / 2, 43, model.primary.c_str());
  } else {
    drawClipped(4, 43, style::kScreenW - 8, model.primary);
  }
  lcd_.setFont(style::kFontRegular);
  const int secondaryWidth = lcd_.getStrWidth(model.secondary.c_str());
  if (secondaryWidth <= style::kScreenW - 8) {
    lcd_.drawStr((style::kScreenW - secondaryWidth) / 2, 52, model.secondary.c_str());
  } else {
    drawClipped(4, 52, style::kScreenW - 8, model.secondary);
  }
}

void DisplayRenderer::drawProgress(const DisplayModel& model) {
  lcd_.setFont(style::kFontHeading);
  drawClipped(style::kProgressX, 27, style::kProgressW, model.primary);
  lcd_.drawFrame(style::kProgressX, style::kProgressY, style::kProgressW, style::kProgressH);
  lcd_.drawBox(style::kProgressX + 2, style::kProgressY + 2,
               ((style::kProgressW - 4) * constrain(model.otaPercent, 0, 100)) / 100,
               style::kProgressH - 4);
  lcd_.setFont(style::kFontRegular);
  drawClipped(style::kProgressX, 53, style::kProgressW, model.secondary);
}

bool DisplayRenderer::isKeyToken(const String& word) {
  if (word.length() != 1) return false;
  return (word[0] >= 'A' && word[0] <= 'D') || word[0] == '#';
}

void DisplayRenderer::drawKeyChip(int& x, int baselineY, char key) {
  const String glyph(key);
  const int glyphWidth = lcd_.getStrWidth(glyph.c_str());
  const int ascent = lcd_.getAscent();
  const int boxWidth = glyphWidth + 2 * style::kChipPadX;
  const int boxTop = baselineY - ascent - style::kChipPadY;
  const int boxHeight = ascent + 2 * style::kChipPadY;
  lcd_.drawRBox(x, boxTop, boxWidth, boxHeight, style::kChipRadius);
  lcd_.setDrawColor(0);
  lcd_.drawStr(x + style::kChipPadX, baselineY, glyph.c_str());
  lcd_.setDrawColor(1);
  x += boxWidth;
}

void DisplayRenderer::drawActions(const String& actionsText) {
  if (actionsText.length() == 0) return;
  lcd_.drawHLine(style::kFooterTextX, style::kFooterDividerY,
                 style::kScreenW - 2 * style::kFooterTextX);
  lcd_.setFont(style::kFontRegular);
  const int spaceWidth = lcd_.getStrWidth(" ");
  int x = style::kFooterTextX;
  int start = 0;
  const int length = static_cast<int>(actionsText.length());
  while (start <= length && x < style::kScreenW) {
    int end = actionsText.indexOf(' ', start);
    if (end < 0) end = length;
    const String word = actionsText.substring(start, end);
    start = end + 1;
    if (word.length() == 0) continue;
    if (isKeyToken(word)) {
      if (x > style::kFooterTextX) x += style::kChipGroupGap;
      drawKeyChip(x, style::kFooterBaselineY, word[0]);
      x += style::kChipLabelGap;
    } else {
      const int remaining = style::kScreenW - x;
      if (lcd_.getStrWidth(word.c_str()) > remaining) break;
      lcd_.drawStr(x, style::kFooterBaselineY, word.c_str());
      x += lcd_.getStrWidth(word.c_str()) + spaceWidth;
    }
  }
}

void DisplayRenderer::drawSplash(const DisplayModel& model) {
  lcd_.setFont(style::kFontValue);
  const int titleWidth = lcd_.getStrWidth(model.primary.c_str());
  lcd_.drawStr(max(0, (style::kScreenW - titleWidth) / 2), 34, model.primary.c_str());
  lcd_.setFont(style::kFontRegular);
  const int subtitleWidth = lcd_.getStrWidth(model.secondary.c_str());
  lcd_.drawStr(max(0, (style::kScreenW - subtitleWidth) / 2), 49, model.secondary.c_str());
}

void DisplayRenderer::render(const DisplayModel& model, const RenderActivity& activity) {
  lcd_.clearBuffer();
  lcd_.setDrawColor(1);
  if (model.mode == DisplayUiMode::Splash) {
    drawSplash(model);
  } else {
    drawHeader(model, activity);
    drawRail(model.rail, activity);
    switch (model.bodyKind) {
      case DisplayBodyKind::Task: drawTask(model, activity); break;
      case DisplayBodyKind::Quantity: drawQuantity(model, activity); break;
      case DisplayBodyKind::Result: drawResult(model); break;
      case DisplayBodyKind::List: drawList(model); break;
      case DisplayBodyKind::Contrast: drawContrast(model); break;
      case DisplayBodyKind::Confirm: drawConfirm(model); break;
      case DisplayBodyKind::Setup: drawSetup(model); break;
      case DisplayBodyKind::Progress: drawProgress(model); break;
    }
    drawActions(model.actions);
  }
  lcd_.sendBuffer();
  normalizeStartLine();
}

}  // namespace hims_scan
