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

void DisplayRenderer::drawHeader(const DisplayModel& model) {
  lcd_.setDrawColor(1);
  lcd_.drawBox(0, 0, 128, 11);
  lcd_.setDrawColor(0);
  lcd_.setFont(u8g2_font_5x7_tf);
  String status;
  if (model.mode == DisplayUiMode::Ota) {
    status = "OTA UPDATE";
  } else {
    switch (model.connectivity) {
      case DisplayConnectivityState::WifiConnecting: status = "CONNECTING TO WIFI"; break;
      case DisplayConnectivityState::PcSearching: status = "CONNECTING TO PC"; break;
      case DisplayConnectivityState::Syncing: status = String("SYNCING Q") + model.queueDepth; break;
      case DisplayConnectivityState::Online:
        status = model.headerAlternate ? "CONNECTED TO PC" : "ONLINE";
        break;
      case DisplayConnectivityState::Offline: status = "OFFLINE"; break;
    }
  }
  if (model.mode == DisplayUiMode::Setup) {
    status = "BLE SETUP";
  }
  drawClipped(3, 8, 122, status);
  lcd_.setDrawColor(1);
}

void DisplayRenderer::drawSplash(const DisplayModel& model) {
  lcd_.setDrawColor(1);
  lcd_.setFont(u8g2_font_7x13B_tf);
  const int titleX = (128 - lcd_.getStrWidth(model.title.c_str())) / 2;
  lcd_.drawStr(max(0, titleX), 30, model.title.c_str());
  lcd_.setFont(u8g2_font_6x10_tf);
  const int subtitleX = (128 - lcd_.getStrWidth(model.line1.c_str())) / 2;
  lcd_.drawStr(max(0, subtitleX), 45, model.line1.c_str());
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
  if (model.mode == DisplayUiMode::Menu || model.mode == DisplayUiMode::QuickLabels ||
      model.mode == DisplayUiMode::QuickSettings) {
    // Menus intentionally use the entire panel: no connectivity bar, scan arrow, or footer competes with controls.
    lcd_.setFont(u8g2_font_6x10_tf);
    drawClipped(4, 10, 120, model.title);
    for (std::size_t index = 0; index < model.menuLineCount && index < 5U; ++index) {
      drawClipped(7, 24 + static_cast<int>(index) * 10, 118, model.menuLines[index]);
    }
    return;
  }
  if (model.mode == DisplayUiMode::Quantity) {
    lcd_.setFont(u8g2_font_5x7_tf);
    drawClipped(x, 18, width, model.title);
    drawClipped(x, 27, width, model.line1);
    lcd_.setFont(u8g2_font_logisoso18_tn);
    lcd_.setCursor(x, 49);
    lcd_.print(model.quantityText);
    const auto prefix = model.quantityText.substring(0, min(model.quantityCursor, model.quantityText.length()));
    const int cursorX = x + lcd_.getStrWidth(prefix.c_str());
    int cursorWidth = 9;
    if (model.quantityCursor < model.quantityText.length()) {
      const auto digit = model.quantityText.substring(model.quantityCursor, model.quantityCursor + 1);
      cursorWidth = max(6, static_cast<int>(lcd_.getStrWidth(digit.c_str())));
    }
    lcd_.drawHLine(cursorX, 50, cursorWidth);
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
  if (model.mode == DisplayUiMode::Splash) {
    drawSplash(model);
    lcd_.sendBuffer();
    normalizeStartLine();
    return;
  }
  const bool fullScreenMenu = model.mode == DisplayUiMode::Menu || model.mode == DisplayUiMode::QuickLabels ||
                              model.mode == DisplayUiMode::QuickSettings;
  if (!fullScreenMenu) drawHeader(model);
  if (!fullScreenMenu && model.mode != DisplayUiMode::Standby) drawArrow(pulseArrow);
  drawBody(model);
  if (!fullScreenMenu) drawFooter(model.footer);
  lcd_.sendBuffer();
  normalizeStartLine();
}

}  // namespace hims_scan
