// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <U8g2lib.h>

#include "display/display_model.h"

namespace inventatory_scan {

class DisplayRenderer {
 public:
  DisplayRenderer();
  void begin();
  void setPowerSave(bool enabled);
  void setContrast(uint8_t contrast);
  void normalizeStartLine();
  void render(const DisplayModel& model, const RenderActivity& activity);

 private:
  void drawHeader(const DisplayModel& model, const RenderActivity& activity);
  void drawRail(DisplayRailStyle rail, const RenderActivity& activity);
  void drawTask(const DisplayModel& model, const RenderActivity& activity);
  void drawQuantity(const DisplayModel& model, const RenderActivity& activity);
  void drawResult(const DisplayModel& model);
  void drawList(const DisplayModel& model);
  void drawContrast(const DisplayModel& model);
  void drawConfirm(const DisplayModel& model);
  void drawSetup(const DisplayModel& model);
  void drawActions(const String& actions);
  void drawMark(int x, int y, int unit, uint8_t dotSlots, bool showEmptySlots,
                int bodyCells = kBrandIntroBodyCells);
  void drawBoot(const DisplayModel& model, const RenderActivity& activity);
  void drawUpdate(const DisplayModel& model, const RenderActivity& activity);
  void drawClipped(int x, int y, int maxWidth, const String& text);
  void drawWrapped(int x, int firstBaselineY, int maxWidth, const String& text,
                   std::size_t maxLines, int lineStep);
  static bool isKeyToken(const String& word);
  void drawKeyChip(int& x, int baselineY, char key);
  String pcStatusText(const DisplayModel& model) const;

  U8G2_ST7565_ERC12864_ALT_F_4W_SW_SPI lcd_;
};

}  // namespace inventatory_scan
