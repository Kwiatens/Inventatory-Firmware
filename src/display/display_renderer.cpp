// Scan R1 LCD rendering routines.
#include "display/display_renderer.h"

#include <algorithm>
#include <cctype>

#include "InventatoryScanUiPolicy.h"
#include "config/hardware.h"
#include "display/display_style.h"

namespace inventatory_scan {

namespace {

// Chevron column for each body row of the ›i mark, top to bottom; the stem
// fills column 4 of the same rows.
constexpr int kChevronColumns[style::kMarkBodyRows] = {0, 1, 2, 1, 0};
constexpr int kStemColumn = 4;

}  // namespace

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
  switch (railStyle) {
    case DisplayRailStyle::Static:
      lcd_.drawBox(style::kRailX, style::kRailTop, style::kRailWidth, style::kRailHeight);
      break;
    case DisplayRailStyle::Activity:
      lcd_.drawBox(style::kRailX,
                   style::kRailTop + constrain(activity.railActivityOffset, 0,
                                                style::kRailActivityTravel),
                   style::kRailWidth, style::kRailActivitySegmentH);
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

bool DisplayRenderer::isKeyToken(const String& word) {
  if (word.length() != 1) return false;
  return (word[0] >= 'A' && word[0] <= 'D') || word[0] == '#';
}

void DisplayRenderer::drawKeyChip(int& x, int baselineY, char key) {
  const String glyph(key);
  const int glyphWidth = lcd_.getStrWidth(glyph.c_str());
  const int ascent = lcd_.getAscent();
  const int boxWidth = glyphWidth + 2 * style::kChipPadX;
  const int boxTop = baselineY - ascent - style::kChipTopPadY;
  const int boxHeight = ascent + style::kChipTopPadY + style::kChipBottomPadY;
  lcd_.drawRBox(x, boxTop, boxWidth, boxHeight, style::kChipRadius);
  lcd_.setDrawColor(0);
  lcd_.drawStr(x + style::kChipPadX, baselineY, glyph.c_str());
  lcd_.setDrawColor(1);
  x += boxWidth;
}

void DisplayRenderer::drawActions(const String& actionsText) {
  if (actionsText.length() == 0) return;
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
      const int wordWidth = lcd_.getStrWidth(word.c_str());
      const int remaining = style::kScreenW - x;
      if (wordWidth > remaining) break;
      lcd_.drawStr(x, style::kFooterBaselineY, word.c_str());
      x += wordWidth + spaceWidth;
    }
  }
}

// bodyCells counts the body in build order: chevron pixels top to bottom, then
// stem rows bottom-up.
void DisplayRenderer::drawMark(int x, int y, int unit, uint8_t dotSlots, bool showEmptySlots,
                               int bodyCells) {
  const int bodyY = y + style::kMarkBodyRow * unit;
  const int chevronRows = constrain(bodyCells, 0, style::kMarkBodyRows);
  for (int row = 0; row < chevronRows; ++row) {
    lcd_.drawBox(x + kChevronColumns[row] * unit, bodyY + row * unit, unit, unit);
  }
  const int stemRows = constrain(bodyCells - style::kMarkBodyRows, 0, style::kMarkBodyRows);
  if (stemRows > 0) {
    lcd_.drawBox(x + kStemColumn * unit, bodyY + (style::kMarkBodyRows - stemRows) * unit, unit,
                 stemRows * unit);
  }

  // The dot is one cell split into four slots; with an odd unit the left and
  // top slots take the extra pixel so a full dot matches the stem's width.
  const int lead = (unit + 1) / 2;
  for (int slot = 0; slot < 4; ++slot) {
    const int column = slot & 1;
    const int row = slot >> 1;
    const int slotX = x + kStemColumn * unit + column * lead;
    const int slotY = y + row * lead;
    const int slotW = column == 0 ? lead : unit - lead;
    const int slotH = row == 0 ? lead : unit - lead;
    if ((dotSlots & (1U << slot)) != 0U) {
      lcd_.drawBox(slotX, slotY, slotW, slotH);
    } else if (showEmptySlots) {
      lcd_.drawPixel(slotX + slotW / 2, slotY + slotH / 2);
    }
  }
}

// Boot: the mark on the left with its body, without the dot, centred on the
// screen; the name beside it, centred on that body; the flashed firmware version
// in the bottom-right corner.
void DisplayRenderer::drawBoot(const DisplayModel& model, const RenderActivity& activity) {
  const int unit = style::kBootMarkUnit;
  const int markY =
      (style::kScreenH - style::kMarkBodyRows * unit) / 2 - style::kMarkBodyRow * unit;
  drawMark(style::kBootMarkX, markY, unit,
           activity.brandDotSlots & brandIntroDot(model.introStep), false, model.introStep);
  if (model.introStep < kBrandIntroSteps) return;

  const int textX = style::kBootMarkX + style::kMarkCols * unit + style::kBootColumnGap;
  const int textW = style::kHeaderRightX - textX;
  const int middle = markY + (style::kMarkBodyRow * unit) + (style::kMarkBodyRows * unit) / 2;
  // The name takes the large face; a longer boot error falls back to the heading.
  lcd_.setFont(style::kFontBrand);
  const uint8_t* const titleFont =
      lcd_.getStrWidth(model.primary.c_str()) <= textW ? style::kFontBrand : style::kFontHeading;
  lcd_.setFont(titleFont);
  const int titleAscent = lcd_.getAscent();
  lcd_.setFont(style::kFontHeading);
  const int detailAscent = lcd_.getAscent();
  const int blockHeight = titleAscent + style::kBootLineGap + detailAscent;
  const int titleY = middle - blockHeight / 2 + titleAscent;
  drawClipped(textX, titleY + style::kBootLineGap + detailAscent, textW, model.secondary);
  lcd_.setFont(titleFont);
  drawClipped(textX, titleY, textW, model.primary);

  lcd_.setFont(style::kFontRegular);
  const int versionWidth = lcd_.getStrWidth(model.tertiary.c_str());
  lcd_.drawStr(style::kHeaderRightX - versionWidth, style::kFooterBaselineY,
               model.tertiary.c_str());
}

void DisplayRenderer::drawUpdate(const DisplayModel& model, const RenderActivity& activity) {
  const int unit = style::kUpdateMarkUnit;
  const int markX = (style::kScreenW - (style::kMarkCols * unit + style::kUpdateColumnGap +
                                        style::kUpdateColumnW)) /
                    2;
  const int bodyHeight = style::kMarkBodyRows * unit;
  const int bodyY = (style::kScreenH - bodyHeight) / 2;
  drawMark(markX, bodyY - style::kMarkBodyRow * unit, unit, activity.brandDotSlots, true);

  const int textX = markX + style::kMarkCols * unit + style::kUpdateColumnGap;
  const int middle = bodyY + bodyHeight / 2;
  lcd_.setFont(style::kFontHeading);
  const int titleAscent = lcd_.getAscent();
  if (model.brandDot == DisplayBrandDot::Progress) {
    lcd_.setFont(style::kFontValue);
    const int valueAscent = lcd_.getAscent();
    const int blockHeight = titleAscent + style::kUpdatePercentGap + valueAscent +
                            style::kUpdateBarGap + style::kUpdateBarBlockH;
    const int titleY = middle - blockHeight / 2 + titleAscent;
    const int valueY = titleY + style::kUpdatePercentGap + valueAscent;
    const int barY = valueY + style::kUpdateBarGap;
    drawClipped(textX, valueY, style::kUpdateColumnW, model.secondary);
    lcd_.setFont(style::kFontHeading);
    drawClipped(textX, titleY, style::kUpdateColumnW, model.primary);
    // Pixel blocks like the website's dashed indicator; unlit blocks keep a floor.
    const int lit = (constrain(model.otaPercent, 0, 100) * style::kUpdateBarBlocks + 50) / 100;
    for (int block = 0; block < style::kUpdateBarBlocks; ++block) {
      const int blockX = textX + block * (style::kUpdateBarBlockW + 1);
      if (block < lit) {
        lcd_.drawBox(blockX, barY, style::kUpdateBarBlockW, style::kUpdateBarBlockH);
      } else {
        lcd_.drawHLine(blockX, barY + style::kUpdateBarBlockH - 1, style::kUpdateBarBlockW);
      }
    }
    return;
  }
  lcd_.setFont(style::kFontRegular);
  const int detailAscent = lcd_.getAscent();
  const int blockHeight = titleAscent + style::kUpdateTitleGap + detailAscent;
  const int titleY = middle - blockHeight / 2 + titleAscent;
  drawClipped(textX, titleY + style::kUpdateTitleGap + detailAscent, style::kUpdateColumnW,
              model.secondary);
  lcd_.setFont(style::kFontHeading);
  drawClipped(textX, titleY, style::kUpdateColumnW, model.primary);
}

void DisplayRenderer::render(const DisplayModel& model, const RenderActivity& activity) {
  lcd_.clearBuffer();
  lcd_.setDrawColor(1);
  if (model.mode == DisplayUiMode::Splash || model.mode == DisplayUiMode::Boot) {
    drawBoot(model, activity);
  } else if (model.mode == DisplayUiMode::Ota) {
    drawUpdate(model, activity);
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
    }
    drawActions(model.actions);
  }
  lcd_.sendBuffer();
  normalizeStartLine();
}

}  // namespace inventatory_scan
