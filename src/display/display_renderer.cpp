// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

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

// Lightning bolt, 5 x 7: one byte per row, bit 0 at the left.
constexpr uint8_t kBoltRows[style::kBoltH] = {0x18, 0x0C, 0x06, 0x1F, 0x18, 0x0C, 0x06};

// Low-battery triangle, 16 x 14, with the '!' cut out; a bitmap keeps the sides even.
// Two bytes per row, low byte first, bit 0 at the left (drawXBMP format).
constexpr uint8_t kWarnIconRows[style::kWarnIconH * 2] = {
    0x80, 0x01,  // .......##.......
    0xC0, 0x03,  // ......####......
    0xC0, 0x03,  // ......####......
    0x60, 0x06,  // .....##..##.....
    0x60, 0x06,  // .....##..##.....
    0x70, 0x0E,  // ....###..###....
    0x70, 0x0E,  // ....###..###....
    0x78, 0x1E,  // ...####..####...
    0xF8, 0x1F,  // ...##########...
    0x7C, 0x3E,  // ..#####..#####..
    0x7C, 0x3E,  // ..#####..#####..
    0xFE, 0x7F,  // .##############.
    0xFE, 0x7F,  // .##############.
    0xFF, 0xFF,  // ################
};

// The chevron's tip row: the middle of the five body rows.
constexpr int kTipRow = style::kMarkBodyRows / 2;

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

// One centred line, clipped to the screen less a margin on each side.
void DisplayRenderer::drawCentered(const uint8_t* font, int baselineY, const String& text) {
  lcd_.setFont(font);
  const int width = std::min(static_cast<int>(lcd_.getStrWidth(text.c_str())),
                             style::kScreenW - 2 * style::kCenterMargin);
  drawClipped((style::kScreenW - width) / 2, baselineY, width, text);
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

// Top bar right slot for one page. Charging replaces the PC status on the status page. An empty
// slot (such as an estimate before a rate is known) makes drawHeader fall back.
DisplayRenderer::HeaderSlot DisplayRenderer::headerSlotFor(const DisplayModel& model, HeaderPage page) {
  HeaderSlot slot;
  if (model.pageText.length() > 0) {
    slot.text = model.pageText;
    slot.font = style::kFontValue;
  } else if (model.batteryPercent < 0) {
    if (model.showPcStatus) {
      slot.text = pcStatusText(model);
      slot.font = style::kFontRegular;
    }
  } else if (page == HeaderPage::Status && model.showPcStatus) {
    slot.text = !model.batteryCharging ? pcStatusText(model)
                : (model.batteryChargeDone || model.batteryPercent >= 100) ? String("Charged")
                                                                           : String("Charging");
    slot.font = style::kFontRegular;
  } else if (page == HeaderPage::Estimate && model.batteryEstimate.length() > 0) {
    slot.text = model.batteryEstimate;
    slot.font = style::kFontRegular;
  } else {
    slot.battery = true;
    slot.charging = model.batteryCharging;
    slot.percent = model.batteryPercent;
    slot.text = String(model.batteryPercent) + "%";
    slot.font = style::kFontRegular;
  }

  if (slot.battery || slot.text.length() > 0) {
    lcd_.setFont(slot.font != nullptr ? slot.font : style::kFontRegular);
    slot.width = lcd_.getStrWidth(slot.text.c_str());
    if (slot.battery) {
      slot.width += style::kBatteryBodyW + style::kBatteryNubW + style::kBatteryIconGap;
      if (slot.charging) slot.width += style::kBoltW + style::kBoltGap;
    }
  }
  return slot;
}

void DisplayRenderer::drawHeaderSlot(const HeaderSlot& slot, int rightX) {
  int x = rightX - slot.width;
  if (!slot.battery) {
    lcd_.setFont(slot.font);
    lcd_.drawStr(x, style::kHeaderBaselineY, slot.text.c_str());
    return;
  }
  if (slot.charging) {
    drawBolt(x, style::kHeaderIconY);
    x += style::kBoltW + style::kBoltGap;
  }
  drawBatteryIcon(x, style::kHeaderIconY, slot.percent);
  x += style::kBatteryBodyW + style::kBatteryNubW + style::kBatteryIconGap;
  lcd_.setFont(style::kFontRegular);
  lcd_.drawStr(x, style::kHeaderBaselineY, slot.text.c_str());
}

// Battery outline with nub, filled from the left by charge; at least 1 px while any charge is left.
void DisplayRenderer::drawBatteryIcon(int x, int y, int percent) {
  const int bodyW = style::kBatteryBodyW;
  const int bodyH = style::kBatteryBodyH;
  lcd_.drawFrame(x, y, bodyW, bodyH);
  lcd_.drawBox(x + bodyW, y + (bodyH - style::kBatteryNubH) / 2, style::kBatteryNubW, style::kBatteryNubH);
  const int innerW = bodyW - 2;
  const int innerH = bodyH - 2;
  const int clamped = constrain(percent, 0, 100);
  int fill = clamped * innerW / 100;
  if (clamped > 0 && fill == 0) fill = 1;
  if (fill > 0) lcd_.drawBox(x + 1, y + 1, fill, innerH);
}

void DisplayRenderer::drawBolt(int x, int y) { lcd_.drawXBMP(x, y, style::kBoltW, style::kBoltH, kBoltRows); }

// Top bar fault "!", drawn in pixels so no font glyph can clip it.
void DisplayRenderer::drawFaultMark(int rightX) {
  const int x = rightX - style::kFaultMarkW + 1;
  lcd_.drawBox(x, style::kFaultMarkY, style::kFaultMarkW, style::kFaultMarkBarH);
  lcd_.drawBox(x, style::kFaultMarkY + style::kFaultMarkBarH + style::kFaultMarkGapH, style::kFaultMarkW,
               style::kFaultMarkDotH);
}

void DisplayRenderer::drawHeader(const DisplayModel& model, const RenderActivity& activity) {
  const String header = model.animateEllipsis
                            ? model.header + (activity.alternate ? "..." : ".")
                            : model.header;
  lcd_.setFont(style::kFontHeading);
  const int titleWidth = lcd_.getStrWidth(header.c_str());
  const int markWidth = model.problemMark ? style::kFaultMarkW : 0;
  const int slotRight = style::kHeaderRightX - (markWidth > 0 ? markWidth + style::kHeaderStatusGap : 0);
  const int available = slotRight - style::kHeaderLeftX;

  const auto page = static_cast<HeaderPage>(activity.headerPage);
  HeaderSlot slot = headerSlotFor(model, page);
  // A long estimate or status gives way to the percentage; the fault mark leaves less room.
  if ((page == HeaderPage::Estimate || page == HeaderPage::Status) &&
      !displayHeaderCanShowStatus(titleWidth, slot.width, available, style::kHeaderStatusGap)) {
    slot = headerSlotFor(model, HeaderPage::Battery);
  }
  const bool showRight =
      slot.width > 0 && displayHeaderCanShowStatus(titleWidth, slot.width, available, style::kHeaderStatusGap);

  lcd_.setFont(style::kFontHeading);
  drawClipped(style::kHeaderLeftX, style::kHeaderBaselineY,
              showRight ? available - slot.width - style::kHeaderStatusGap : available, header);
  if (showRight) drawHeaderSlot(slot, slotRight);
  if (markWidth > 0) drawFaultMark(style::kHeaderRightX);
  // The divider spans the whole bar, fault mark included.
  lcd_.drawHLine(style::kHeaderLeftX, style::kHeaderDividerY, style::kHeaderRightX - style::kHeaderLeftX);
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

void DisplayRenderer::drawTask(const DisplayModel& model) {
  const String& primary = model.primary;
  lcd_.setFont(style::kFontHeading);
  const bool primaryFits = lcd_.getStrWidth(primary.c_str()) <= style::kContentW;
  if (primaryFits) {
    drawClipped(style::kContentX, style::kTaskPrimaryY, style::kContentW, primary);
  } else {
    lcd_.setFont(style::kFontRegular);
    drawWrapped(style::kContentX, style::kTaskWrapY, style::kContentW, primary, 2, style::kTextLineStep);
  }
  const int secondaryY = primaryFits ? style::kTaskSecondaryY : style::kTaskWrapSecondaryY;
  // A fault shows its error code in place of the normal text.
  if (model.errorCode.length() > 0) {
    drawErrorCodeLine(style::kContentX, secondaryY, model.errorCode);
    return;
  }
  lcd_.setFont(style::kFontRegular);
  drawWrapped(style::kContentX, secondaryY, style::kContentW, model.secondary, 1, style::kTextLineStep);
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
    drawWrapped(style::kContentX, style::kQuantityWrapY, style::kContentW, model.primary, 2, style::kTextLineStep);
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
    drawWrapped(style::kContentX, style::kResultWrapY, style::kContentW, model.primary, 2, style::kTextLineStep);
  }
  lcd_.setFont(style::kFontRegular);
  drawClipped(style::kContentX, primaryFits ? style::kResultSecondaryY : style::kResultWrapSecondaryY,
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

// "Error code" in the regular face, with the code after it in bold, on one baseline.
void DisplayRenderer::drawErrorCodeLine(int x, int baselineY, const String& code) {
  static const char kLabel[] = "Error code ";
  lcd_.setFont(style::kFontRegular);
  lcd_.drawStr(x, baselineY, kLabel);
  const int labelWidth = lcd_.getStrWidth(kLabel);
  lcd_.setFont(style::kFontHeading);
  drawClipped(x + labelWidth, baselineY, style::kHeaderRightX - x - labelWidth, code);
}

// Fault screen: error code on the first line, explanation on the last two list rows.
void DisplayRenderer::drawFault(const DisplayModel& model) {
  const int width = style::kScreenW - 2 * style::kListX;
  drawErrorCodeLine(style::kListX, style::kListFirstY, model.errorCode);
  const int messageY = style::kListFirstY + (static_cast<int>(style::kListMaxRows) - 2) * style::kListRowStep;
  lcd_.setFont(style::kFontRegular);
  drawClipped(style::kListX, messageY, width, model.secondary);
  drawClipped(style::kListX, messageY + style::kListRowStep, width, model.tertiary);
}

void DisplayRenderer::drawContrast(const DisplayModel& model) {
  drawCentered(style::kFontValue, style::kContrastValueY, model.primary);
  lcd_.drawFrame(style::kContrastBarX, style::kContrastBarY, style::kContrastBarW, style::kContrastBarH);
  const int level = constrain(model.primary.toInt(), 0, 63);
  lcd_.drawBox(style::kContrastBarX + 2, style::kContrastBarY + 2, ((style::kContrastBarW - 4) * level) / 63, 1);
}

// Option name, one segment per option (the chosen one filled), then what it does.
void DisplayRenderer::drawChoice(const DisplayModel& model) {
  drawCentered(style::kFontValue, style::kChoiceLabelY, model.primary);

  const int count = static_cast<int>(model.choiceCount);
  const int total = count * style::kChoiceSegmentW + (count > 0 ? (count - 1) * style::kChoiceSegmentGap : 0);
  const int startX = (style::kScreenW - total) / 2;
  for (int index = 0; index < count; ++index) {
    const int x = startX + index * (style::kChoiceSegmentW + style::kChoiceSegmentGap);
    if (static_cast<std::size_t>(index) == model.choiceIndex) {
      lcd_.drawBox(x, style::kChoiceSegmentY, style::kChoiceSegmentW, style::kChoiceSegmentH);
    } else {
      lcd_.drawFrame(x, style::kChoiceSegmentY, style::kChoiceSegmentW, style::kChoiceSegmentH);
    }
  }
  drawCentered(style::kFontRegular, style::kChoiceDetailY, model.secondary);
}

void DisplayRenderer::drawConfirm(const DisplayModel& model) {
  const int width = style::kScreenW - 2 * style::kListX;
  lcd_.setFont(style::kFontHeading);
  drawClipped(style::kListX, style::kConfirmPrimaryY, width, model.primary);
  lcd_.setFont(style::kFontRegular);
  drawWrapped(style::kListX, style::kConfirmSecondaryY, width, model.secondary, 1, style::kTextLineStep);
}

void DisplayRenderer::drawSetup(const DisplayModel& model) {
  char code[7];
  snprintf(code, sizeof(code), "%06lu", static_cast<unsigned long>(model.setupCode));
  drawCentered(style::kFontValue, style::kSetupCodeY, code);
  drawCentered(style::kFontHeading, style::kSetupPrimaryY, model.primary);
  drawCentered(style::kFontRegular, style::kSetupSecondaryY, model.secondary);
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

// The ›i mark: the chevron and the stem, from the top of its body.
void DisplayRenderer::drawMark(int x, int bodyY, int unit) {
  for (int row = 0; row < style::kMarkBodyRows; ++row) {
    lcd_.drawBox(x + kChevronColumns[row] * unit, bodyY + row * unit, unit, unit);
  }
  lcd_.drawBox(x + kStemColumn * unit, bodyY, unit, style::kMarkBodyRows * unit);
}

// Boot: mark on the left, name beside it, firmware version in the bottom-right corner.
void DisplayRenderer::drawBoot(const DisplayModel& model, const RenderActivity& activity) {
  const int unit = style::kBootMarkUnit;
  const int bodyY = (style::kScreenH - style::kMarkBodyRows * unit) / 2;
  // The mark blooms in on wake; the name and version wait until it is whole.
  if (activity.bloomStage < kBootBloomStages) {
    drawMarkBloom(style::kBootMarkX, bodyY, unit, activity.bloomStage);
    return;
  }
  drawMark(style::kBootMarkX, bodyY, unit);

  const int textX = style::kBootMarkX + style::kMarkCols * unit + style::kBootColumnGap;
  const int textW = style::kHeaderRightX - textX;
  const int middle = bodyY + (style::kMarkBodyRows * unit) / 2;
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

void DisplayRenderer::drawUpdate(const DisplayModel& model) {
  const int unit = style::kUpdateMarkUnit;
  const int markX = (style::kScreenW - (style::kMarkCols * unit + style::kUpdateColumnGap +
                                        style::kUpdateColumnW)) /
                    2;
  const int bodyHeight = style::kMarkBodyRows * unit;
  const int bodyY = (style::kScreenH - bodyHeight) / 2;
  drawMark(markX, bodyY, unit);

  const int textX = markX + style::kMarkCols * unit + style::kUpdateColumnGap;
  const int middle = bodyY + bodyHeight / 2;
  lcd_.setFont(style::kFontHeading);
  const int titleAscent = lcd_.getAscent();
  if (model.otaProgress) {
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
    // Pixel blocks; unlit blocks keep a one-line floor.
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

// Full-screen card on dock connect or removal: fill rises in steps; a bolt marks charging.
void DisplayRenderer::drawBatteryCard(const DisplayModel& model, const RenderActivity& activity) {
  const int x = style::kCardBatteryX;
  const int y = style::kCardBatteryY;
  const int w = style::kCardBatteryW;
  const int h = style::kCardBatteryH;
  lcd_.drawFrame(x, y, w, h);
  lcd_.drawBox(x + w, y + (h - style::kCardNubH) / 2, style::kCardNubW, style::kCardNubH);

  const int innerX = x + 3;
  const int innerY = y + 3;
  const int innerW = w - 6;
  const int innerH = h - 6;
  const int percent = constrain(model.batteryPercent, 0, 100);
  const int fill = innerW * percent * activity.cardFillStep / (100 * kBatteryCardFillSteps);
  if (fill > 0) lcd_.drawBox(innerX, innerY, fill, innerH);
  if (model.batteryCharging) {
    // XOR, so the bolt reads over both the fill and the empty body.
    lcd_.setDrawColor(2);
    lcd_.drawXBMP(x + (w - style::kBoltW) / 2, y + (h - style::kBoltH) / 2, style::kBoltW, style::kBoltH,
                  kBoltRows);
    lcd_.setDrawColor(1);
  }

  const int textW = style::kHeaderRightX - style::kCardTextX;
  lcd_.setFont(style::kFontHeading);
  drawClipped(style::kCardTextX, style::kCardTitleY, textW, model.primary);
  lcd_.setFont(style::kFontRegular);
  drawClipped(style::kCardTextX, style::kCardDetailY, textW, model.secondary);
  lcd_.setFont(style::kFontValue);
  const String percentText = model.batteryPercent >= 0 ? String(model.batteryPercent) + "%" : String("--%");
  drawClipped(style::kCardTextX, style::kCardValueY, textW, percentText);
}

// Low-battery warning: triangle, title, detail, a bar that empties toward shutdown, countdown line.
void DisplayRenderer::drawCountdownWarning(const DisplayModel& model) {
  lcd_.drawXBMP(style::kWarnIconX, style::kWarnIconY, style::kWarnIconW, style::kWarnIconH, kWarnIconRows);

  const int textW = style::kHeaderRightX - style::kWarnTextX;
  lcd_.setFont(style::kFontHeading);
  drawClipped(style::kWarnTextX, style::kWarnTitleY, textW, model.primary);
  lcd_.setFont(style::kFontRegular);
  drawClipped(style::kWarnTextX, style::kWarnDetailY, textW, model.secondary);

  const bool hasBar = model.countdownSeconds >= 0;
  drawClipped(style::kWarnTextX, hasBar ? style::kWarnCountdownY : style::kWarnNoticeY, textW, model.tertiary);
  if (hasBar) {
    const int fill = style::kWarnBarW * constrain(model.countdownPercent, 0, 100) / 100;
    if (fill > 0) lcd_.drawBox(style::kWarnBarX, style::kWarnBarY, fill, style::kWarnBarH);
  }
}

// Soft power-off warning: title, draining countdown bar, hint. The bar is the only countdown.
void DisplayRenderer::drawPowerWarning(const DisplayModel& model, const RenderActivity& activity) {
  drawCentered(style::kFontHeading, style::kPowerTitleY, model.primary);
  lcd_.drawFrame(style::kPowerBarX, style::kPowerBarY, style::kPowerBarW, style::kPowerBarH);
  if (activity.powerBarFillPx > 0) {
    lcd_.drawBox(style::kPowerBarX + 2, style::kPowerBarY + 2, activity.powerBarFillPx, style::kPowerBarH - 4);
  }
  drawCentered(style::kFontRegular, style::kPowerHintY, model.secondary);
}

// Power-off ending: the mark, offset by markOffsetY and cut at the glass rule, with one line below.
void DisplayRenderer::drawPowerOff(const DisplayModel& model, const RenderActivity& activity) {
  drawMarkBehindGlass(style::kPowerOffMarkX, style::kPowerOffMarkY + activity.markOffsetY, style::kBootMarkUnit);
  lcd_.drawHLine((style::kScreenW - style::kGlassEdgeW) / 2, style::kGlassEdgeY, style::kGlassEdgeW);
  drawCentered(style::kFontRegular, style::kPowerOffTextY, model.primary);
}

// Wake bloom: the stem grows over stages 1-3; the chevron draws out from its tip over stages 4-6.
// Stage 0 is blank.
void DisplayRenderer::drawMarkBloom(int x, int bodyY, int unit, int stage) {
  const int stemRows = stage >= 3 ? style::kMarkBodyRows : stage == 2 ? 3 : stage == 1 ? 1 : 0;
  if (stemRows > 0) lcd_.drawBox(x + kStemColumn * unit, bodyY, unit, stemRows * unit);

  // Rows within reach of the tip: stage 4 draws the tip alone, stage 5 adds its neighbours.
  const int reach = stage - 4;
  if (reach < 0) return;
  for (int row = 0; row < style::kMarkBodyRows; ++row) {
    const int distance = row > kTipRow ? row - kTipRow : kTipRow - row;
    if (distance <= reach) {
      lcd_.drawBox(x + kChevronColumns[row] * unit, bodyY + row * unit, unit, unit);
    }
  }
}

// The chevron and stem, each cell cut at the glass edge so the mark slides behind it.
void DisplayRenderer::drawMarkBehindGlass(int x, int bodyY, int unit) {
  for (int row = 0; row < style::kMarkBodyRows; ++row) {
    drawBoxAboveGlass(x + kChevronColumns[row] * unit, bodyY + row * unit, unit, unit);
  }
  drawBoxAboveGlass(x + kStemColumn * unit, bodyY, unit, style::kMarkBodyRows * unit);
}

// A box drawn only where it is above the glass edge.
void DisplayRenderer::drawBoxAboveGlass(int x, int y, int w, int h) {
  const int bottom = std::min(y + h, style::kGlassEdgeY);
  if (bottom > y) lcd_.drawBox(x, y, w, bottom - y);
}

void DisplayRenderer::render(const DisplayModel& model, const RenderActivity& activity) {
  lcd_.clearBuffer();
  lcd_.setDrawColor(1);
  switch (model.mode) {
    case DisplayUiMode::Splash:
    case DisplayUiMode::Boot:
      drawBoot(model, activity);
      break;
    case DisplayUiMode::Ota:
      drawUpdate(model);
      break;
    case DisplayUiMode::BatteryCard:
      drawBatteryCard(model, activity);
      break;
    case DisplayUiMode::CountdownWarning:
      drawCountdownWarning(model);
      break;
    case DisplayUiMode::PowerWarning:
      drawPowerWarning(model, activity);
      break;
    case DisplayUiMode::PowerOff:
      drawPowerOff(model, activity);
      break;
    case DisplayUiMode::Blank:
      break;
    default:
      drawHeader(model, activity);
      drawRail(model.rail, activity);
      switch (model.bodyKind) {
        case DisplayBodyKind::Task: drawTask(model); break;
        case DisplayBodyKind::Quantity: drawQuantity(model, activity); break;
        case DisplayBodyKind::Result: drawResult(model); break;
        case DisplayBodyKind::List: drawList(model); break;
        case DisplayBodyKind::Fault: drawFault(model); break;
        case DisplayBodyKind::Contrast: drawContrast(model); break;
        case DisplayBodyKind::Choice: drawChoice(model); break;
        case DisplayBodyKind::Confirm: drawConfirm(model); break;
        case DisplayBodyKind::Setup: drawSetup(model); break;
      }
      drawActions(model.actions);
      break;
  }
  lcd_.sendBuffer();
  normalizeStartLine();
}

}  // namespace inventatory_scan
