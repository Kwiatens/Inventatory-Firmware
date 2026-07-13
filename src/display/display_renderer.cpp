#include "display/display_renderer.h"

#include "config/hardware.h"

namespace hims_scan {

DisplayRenderer::DisplayRenderer()
    : lcd_(U8G2_R0, hardware::kLcdClockPin, hardware::kLcdDataPin, hardware::kLcdCsPin,
           hardware::kLcdDcPin, hardware::kLcdResetPin) {}

void DisplayRenderer::begin() {
  lcd_.begin();
  lcd_.setContrast(hardware::kLcdContrast);
  lcd_.setPowerSave(0);
}

void DisplayRenderer::setPowerSave(bool enabled) { lcd_.setPowerSave(enabled ? 1 : 0); }

void DisplayRenderer::drawClipped(int x, int y, int maxWidth, const String& text) {
  String clipped = text;
  while (clipped.length() > 0 && lcd_.getStrWidth(clipped.c_str()) > maxWidth) clipped.remove(clipped.length() - 1);
  if (clipped.length() < text.length() && clipped.length() > 3) {
    clipped.remove(clipped.length() - 3);
    clipped += "...";
  }
  lcd_.drawStr(x, y, clipped.c_str());
}

void DisplayRenderer::drawHeader(const DisplayModel& model) {
  lcd_.setDrawColor(1);
  lcd_.drawBox(0, 0, 128, 11);
  lcd_.setDrawColor(0);
  lcd_.setFont(u8g2_font_5x7_tf);
  lcd_.drawStr(3, 8, "HIMS SCAN");
  lcd_.drawStr(79, 8, model.wifiOnline ? "W+" : "W-");
  lcd_.drawStr(94, 8, model.pcOnline ? "P+" : "P-");
  lcd_.setCursor(109, 8);
  lcd_.print('Q');
  lcd_.print(model.queueDepth);
  lcd_.setDrawColor(1);
}

void DisplayRenderer::drawArrow(bool filled) {
  if (filled) {
    lcd_.drawTriangle(1, 32, 11, 23, 11, 41);
  } else {
    lcd_.drawLine(1, 32, 10, 23);
    lcd_.drawLine(1, 32, 10, 41);
    lcd_.drawVLine(10, 23, 19);
  }
}

void DisplayRenderer::drawBody(const DisplayModel& model) {
  constexpr int x = 15;
  constexpr int width = 111;
  if (model.mode == DisplayUiMode::Quantity) {
    lcd_.setFont(u8g2_font_5x7_tf);
    drawClipped(x, 20, width, model.title);
    lcd_.setFont(u8g2_font_logisoso18_tn);
    lcd_.setCursor(x, 45);
    lcd_.print(model.quantity);
    return;
  }
  if (model.mode == DisplayUiMode::Ota) {
    lcd_.setFont(u8g2_font_6x10_tf);
    drawClipped(x, 25, width, model.title);
    lcd_.drawFrame(x, 32, 106, 10);
    lcd_.drawBox(x + 2, 34, (102 * constrain(model.otaPercent, 0, 100)) / 100, 6);
    return;
  }

  lcd_.setFont(u8g2_font_6x10_tf);
  drawClipped(x, 23, width, model.title);
  lcd_.setFont(u8g2_font_5x7_tf);
  drawClipped(x, 35, width, model.line1);
  drawClipped(x, 46, width, model.line2);
}

void DisplayRenderer::drawFooter(const String& footer) {
  lcd_.drawHLine(0, 52, 128);
  lcd_.setFont(u8g2_font_5x7_tf);
  drawClipped(3, 62, 123, footer);
}

void DisplayRenderer::render(const DisplayModel& model, bool pulseArrow) {
  lcd_.clearBuffer();
  drawHeader(model);
  if (model.mode != DisplayUiMode::Standby) drawArrow(pulseArrow);
  drawBody(model);
  drawFooter(model.footer);
  lcd_.sendBuffer();
}

}  // namespace hims_scan
