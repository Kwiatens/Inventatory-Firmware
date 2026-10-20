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
  // Top bar right-hand slot; width 0 when empty.
  struct HeaderSlot {
    String text;
    const uint8_t* font = nullptr;
    bool battery = false;
    bool charging = false;
    int percent = 0;
    int width = 0;
  };

  void drawHeader(const DisplayModel& model, const RenderActivity& activity);
  HeaderSlot headerSlotFor(const DisplayModel& model, HeaderPage page);
  void drawHeaderSlot(const HeaderSlot& slot, int rightX);
  void drawFaultMark(int rightX);
  void drawBatteryIcon(int x, int y, int percent);
  void drawBolt(int x, int y);
  void drawRail(DisplayRailStyle rail, const RenderActivity& activity);
  void drawTask(const DisplayModel& model);
  void drawQuantity(const DisplayModel& model, const RenderActivity& activity);
  void drawResult(const DisplayModel& model);
  void drawList(const DisplayModel& model);
  void drawFault(const DisplayModel& model);
  void drawErrorCodeLine(int x, int baselineY, const String& code);
  void drawContrast(const DisplayModel& model);
  void drawChoice(const DisplayModel& model);
  void drawConfirm(const DisplayModel& model);
  void drawSetup(const DisplayModel& model);
  void drawActions(const String& actions);
  void drawMark(int x, int bodyY, int unit);
  void drawBoot(const DisplayModel& model, const RenderActivity& activity);
  void drawUpdate(const DisplayModel& model);
  void drawBatteryCard(const DisplayModel& model, const RenderActivity& activity);
  void drawCountdownWarning(const DisplayModel& model);
  void drawPowerWarning(const DisplayModel& model, const RenderActivity& activity);
  void drawPowerOff(const DisplayModel& model, const RenderActivity& activity);
  void drawMarkBloom(int x, int bodyY, int unit, int stage);
  void drawMarkBehindGlass(int x, int bodyY, int unit);
  void drawBoxAboveGlass(int x, int y, int w, int h);
  void drawClipped(int x, int y, int maxWidth, const String& text);
  void drawCentered(const uint8_t* font, int baselineY, const String& text);
  void drawWrapped(int x, int firstBaselineY, int maxWidth, const String& text,
                   std::size_t maxLines, int lineStep);
  static bool isKeyToken(const String& word);
  void drawKeyChip(int& x, int baselineY, char key);
  String pcStatusText(const DisplayModel& model) const;

  U8G2_ST7565_ERC12864_ALT_F_4W_SW_SPI lcd_;
};

}  // namespace inventatory_scan
