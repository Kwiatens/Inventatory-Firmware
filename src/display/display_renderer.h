#pragma once

#include <U8g2lib.h>

#include "display/display_model.h"

namespace hims_scan {

class DisplayRenderer {
 public:
  DisplayRenderer();
  void begin();
  void setPowerSave(bool enabled);
  void render(const DisplayModel& model, bool pulseArrow);

 private:
  void drawHeader(const DisplayModel&);
  void drawSplash(const DisplayModel& model);
  void drawArrow(bool filled);
  void drawBody(const DisplayModel& model);
  void drawFooter(const String& footer);
  void drawClipped(int x, int y, int maxWidth, const String& text);

  U8G2_ST7565_ERC12864_ALT_F_4W_SW_SPI lcd_;
};

}  // namespace hims_scan
