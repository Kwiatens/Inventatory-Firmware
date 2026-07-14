#pragma once

#include <U8g2lib.h>

#include "display/display_model.h"

namespace hims_scan {

class DisplayRenderer {
 public:
  DisplayRenderer();
  void begin();
  void setPowerSave(bool enabled);
  void setContrast(uint8_t contrast);
  void normalizeStartLine();
  void render(const DisplayModel& model, const RenderAnim& anim);

 private:
  void drawHeader(const DisplayModel&, const RenderAnim& anim);
  void drawStatusIcons(const DisplayModel&, const RenderAnim& anim);
  void drawSplash(const DisplayModel& model);
  void drawArrow(const RenderAnim& anim);
  void drawBody(const DisplayModel& model, const RenderAnim& anim);
  void drawResult(const DisplayModel& model, const RenderAnim& anim);
  void drawFooter(const String& footer);
  void drawClipped(int x, int y, int maxWidth, const String& text);

  // Small hand-drawn 1-bit glyphs (no icon-font tables pulled into flash).
  void drawWifi(int x, int top, bool online, bool connecting, const RenderAnim& anim);
  void drawMonitor(int x, int top, bool filled);
  void drawSpinner(int cx, int cy, uint8_t frame);
  void drawCheck(int cx, int cy, int r, uint8_t progress);
  void drawCross(int cx, int cy, int r);
  void drawWarningBang(int cx, int cy, int r);

  // Footer key-hint chips: an inverted rounded box behind a single A-D key
  // letter, advancing the cursor. Non-key words render as plain text.
  static bool isKeyToken(const String& word);
  void drawKeyChip(int& x, int baselineY, char key);

  U8G2_ST7565_ERC12864_ALT_F_4W_SW_SPI lcd_;
};

}  // namespace hims_scan
