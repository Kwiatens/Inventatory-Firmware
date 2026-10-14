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

// The top bar's right slot for one page. While charging, the status page reads
// "Charging" in place of the PC status. A page with nothing to show, such as the
// estimate before a rate is known, returns an empty slot; drawHeader then falls back.
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
                : model.batteryPercent >= 100 ? String("Charged")
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

// A battery outline with its nub, filled from the left in proportion to the
// charge. The fill is at least one pixel while any charge is left.
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

// The top bar's fault mark: a "!" two pixels wide, drawn in pixels so that no font glyph can clip it. The
// status slot ends just left of it.
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
  const int markWidth = model.scannerFault ? style::kFaultMarkW : 0;
  const int slotRight = style::kHeaderRightX - (markWidth > 0 ? markWidth + style::kHeaderStatusGap : 0);
  const int available = slotRight - style::kHeaderLeftX;

  const auto page = static_cast<HeaderPage>(activity.headerPage);
  HeaderSlot slot = headerSlotFor(model, page);
  // A long runtime line or status gives way to the percentage rather than to nothing. The fault
  // mark leaves less room, so the status is the line that most often has to give way.
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
  // The divider always spans the whole bar, under the fault mark as well.
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
  const int secondaryY = primaryFits ? style::kTaskSecondaryY : 48;
  // A fault puts its error code on the line below the problem, in place of the normal text.
  if (model.errorCode.length() > 0) {
    drawErrorCodeLine(style::kContentX, secondaryY, model.errorCode);
    return;
  }
  lcd_.setFont(style::kFontRegular);
  drawWrapped(style::kContentX, secondaryY, style::kContentW, model.secondary, 1, 9);
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

// "Error code" in the regular face, with the code after it in bold, on one baseline.
void DisplayRenderer::drawErrorCodeLine(int x, int baselineY, const String& code) {
  static const char kLabel[] = "Error code ";
  lcd_.setFont(style::kFontRegular);
  lcd_.drawStr(x, baselineY, kLabel);
  const int labelWidth = lcd_.getStrWidth(kLabel);
  lcd_.setFont(style::kFontHeading);
  drawClipped(x + labelWidth, baselineY, style::kHeaderRightX - x - labelWidth, code);
}

// The fault screen: the error code on the first line, and the explanation on the last two lines of the
// list.
void DisplayRenderer::drawFault(const DisplayModel& model) {
  const int width = style::kScreenW - 2 * style::kListX;
  drawErrorCodeLine(style::kListX, style::kListFirstY, model.errorCode);
  const int messageY = style::kListFirstY + (static_cast<int>(style::kListMaxRows) - 2) * style::kListRowStep;
  lcd_.setFont(style::kFontRegular);
  drawClipped(style::kListX, messageY, width, model.secondary);
  drawClipped(style::kListX, messageY + style::kListRowStep, width, model.tertiary);
}

void DisplayRenderer::drawContrast(const DisplayModel& model) {
  lcd_.setFont(style::kFontValue);
  const int width = lcd_.getStrWidth(model.primary.c_str());
  lcd_.drawStr((style::kScreenW - width) / 2, 38, model.primary.c_str());
  lcd_.drawFrame(29, 43, 70, 5);
  const int level = constrain(model.primary.toInt(), 0, 63);
  lcd_.drawBox(31, 45, (66 * level) / 63, 1);
}

// The option name, then one segment per option with the chosen one filled, then
// what the option does.
void DisplayRenderer::drawBacklight(const DisplayModel& model) {
  lcd_.setFont(style::kFontValue);
  const int labelWidth = lcd_.getStrWidth(model.primary.c_str());
  lcd_.drawStr((style::kScreenW - labelWidth) / 2, style::kChoiceLabelY, model.primary.c_str());

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

  lcd_.setFont(style::kFontRegular);
  const int detailWidth = lcd_.getStrWidth(model.secondary.c_str());
  lcd_.drawStr((style::kScreenW - detailWidth) / 2, style::kChoiceDetailY, model.secondary.c_str());
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

// The ›i mark: the chevron and the stem, with the dot in the four slots that
// dotSlots selects. showEmptySlots marks the unlit slots with a pixel.
void DisplayRenderer::drawMark(int x, int y, int unit, uint8_t dotSlots, bool showEmptySlots) {
  const int bodyY = y + style::kMarkBodyRow * unit;
  for (int row = 0; row < style::kMarkBodyRows; ++row) {
    lcd_.drawBox(x + kChevronColumns[row] * unit, bodyY + row * unit, unit, unit);
  }
  lcd_.drawBox(x + kStemColumn * unit, bodyY, unit, style::kMarkBodyRows * unit);

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

// Boot: the mark on the left, centred vertically, with the name beside it and
// the flashed firmware version in the bottom-right corner. The normal boot draws
// no dot; a fault draws its static broken dot.
void DisplayRenderer::drawBoot(const DisplayModel& model, const RenderActivity& activity) {
  const int unit = style::kBootMarkUnit;
  const int markY =
      (style::kScreenH - style::kMarkBodyRows * unit) / 2 - style::kMarkBodyRow * unit;
  // The mark blooms in on wake; the name and version wait until it is whole.
  if (activity.bloomStage < kBootBloomStages) {
    drawMarkBloom(style::kBootMarkX, markY, unit, activity.bloomStage);
    return;
  }
  drawMark(style::kBootMarkX, markY, unit, activity.brandDotSlots, false);

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

// The full-screen card that appears when the dock is connected or removed. The
// fill rises in steps, and a bolt marks charging.
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

// The low-battery warning: a triangle, the title, the detail, and a countdown
// line with a bar that empties as the shutdown nears.
void DisplayRenderer::drawLowBattery(const DisplayModel& model) {
  const int x = style::kWarnIconX;
  const int y = style::kWarnIconY;
  const int w = style::kWarnIconW;
  const int h = style::kWarnIconH;
  lcd_.drawTriangle(x + w / 2, y, x, y + h, x + w, y + h);
  lcd_.setDrawColor(0);
  lcd_.drawBox(x + w / 2 - 1, y + 4, 2, 6);
  lcd_.drawBox(x + w / 2 - 1, y + 11, 2, 2);
  lcd_.setDrawColor(1);

  const int textW = style::kHeaderRightX - style::kWarnTextX;
  lcd_.setFont(style::kFontHeading);
  drawClipped(style::kWarnTextX, style::kWarnTitleY, textW, model.primary);
  lcd_.setFont(style::kFontRegular);
  drawClipped(style::kWarnTextX, style::kWarnDetailY, textW, model.secondary);
  drawClipped(style::kWarnBarX, style::kWarnCountdownY, style::kWarnBarW, model.tertiary);

  if (model.countdownSeconds >= 0) {
    lcd_.drawFrame(style::kWarnBarX, style::kWarnBarY, style::kWarnBarW, style::kWarnBarH);
    const int innerW = style::kWarnBarW - 4;
    const int fill = innerW * constrain(model.countdownPercent, 0, 100) / 100;
    if (fill > 0) lcd_.drawBox(style::kWarnBarX + 2, style::kWarnBarY + 2, fill, style::kWarnBarH - 4);
  }
}

// The soft power-off warning: the ›i on the left with no dot, the title and detail beside it, and a bar
// that drains as the countdown runs down. The bar is the only countdown on the screen.
void DisplayRenderer::drawPowerWarning(const DisplayModel& model, const RenderActivity& activity) {
  const int unit = style::kBootMarkUnit;
  const int markY = (style::kScreenH - style::kMarkBodyRows * unit) / 2 - style::kMarkBodyRow * unit;
  drawMark(style::kBootMarkX, markY, unit, 0, false);

  const int textX = style::kBootMarkX + style::kMarkCols * unit + style::kBootColumnGap;
  const int textW = style::kHeaderRightX - textX;
  lcd_.setFont(style::kFontHeading);
  drawClipped(textX, style::kPowerTitleY, textW, model.primary);
  lcd_.setFont(style::kFontRegular);
  drawClipped(textX, style::kPowerDetailY, textW, model.secondary);

  lcd_.drawFrame(style::kPowerBarX, style::kPowerBarY, style::kPowerBarW, style::kPowerBarH);
  if (activity.powerBarFillPx > 0) {
    lcd_.drawBox(style::kPowerBarX + 2, style::kPowerBarY + 2, activity.powerBarFillPx, style::kPowerBarH - 4);
  }
}

// The power-off ending: the ›i, dropped by activity.markOffsetY and cut at the glass edge, with the
// text beside it. The rule under the glass is drawn last.
void DisplayRenderer::drawPowerOff(const DisplayModel& model, const RenderActivity& activity) {
  const int unit = style::kBootMarkUnit;
  const int markY = (style::kScreenH - style::kMarkBodyRows * unit) / 2 - style::kMarkBodyRow * unit;
  drawMarkBehindGlass(style::kBootMarkX, markY + activity.markOffsetY, unit);

  const int textX = style::kBootMarkX + style::kMarkCols * unit + style::kBootColumnGap;
  const int textW = style::kHeaderRightX - textX;
  lcd_.setFont(style::kFontHeading);
  drawClipped(textX, style::kPowerTitleY, textW, model.primary);
  lcd_.setFont(style::kFontRegular);
  drawClipped(textX, style::kPowerDetailY, textW, model.secondary);
  lcd_.drawBox(0, style::kGlassEdgeY, style::kScreenW, 1);
}

// The wake bloom: stage 1 draws the top of the stem, stage 3 the whole stem, then the chevron draws out
// from its tip, the middle arms first, and stage 6 completes it. Stage 0 is blank.
void DisplayRenderer::drawMarkBloom(int x, int y, int unit, int stage) {
  const int bodyY = y + style::kMarkBodyRow * unit;
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

// The chevron and stem with no dot, each cell cut at the glass edge so the mark slides behind it.
void DisplayRenderer::drawMarkBehindGlass(int x, int y, int unit) {
  const int bodyY = y + style::kMarkBodyRow * unit;
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
      drawUpdate(model, activity);
      break;
    case DisplayUiMode::BatteryCard:
      drawBatteryCard(model, activity);
      break;
    case DisplayUiMode::LowBattery:
      drawLowBattery(model);
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
        case DisplayBodyKind::Task: drawTask(model, activity); break;
        case DisplayBodyKind::Quantity: drawQuantity(model, activity); break;
        case DisplayBodyKind::Result: drawResult(model); break;
        case DisplayBodyKind::List: drawList(model); break;
        case DisplayBodyKind::Fault: drawFault(model); break;
        case DisplayBodyKind::Contrast: drawContrast(model); break;
        case DisplayBodyKind::Backlight: drawBacklight(model); break;
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
