// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#include <Arduino.h>
#include <U8g2lib.h>

// GMG12864-06D wiring; Waveshare header labels in the trailing comments.
constexpr uint8_t kLcdClockPin = 13;  // SCL, A6
constexpr uint8_t kLcdDcPin = 11;     // RS, A4
constexpr uint8_t kLcdResetPin = 12;  // RSE, A5
constexpr uint8_t kLcdCsPin = 21;     // CS, D10
constexpr uint8_t kLcdDataPin = 14;   // SI, A7

// 128 x 64 landscape (U8G2_R0), as in display_style.h.
constexpr uint8_t kScreenWidth = 128;
constexpr uint8_t kScreenHeight = 64;

// The standard ST7565 configuration can turn the whole panel dark; use the ALT variant.
U8G2_ST7565_ERC12864_ALT_F_4W_SW_SPI lcd(
    U8G2_R0,
    /* clock=*/kLcdClockPin,
    /* data=*/kLcdDataPin,
    /* cs=*/kLcdCsPin,
    /* dc=*/kLcdDcPin,
    /* reset=*/kLcdResetPin);

void drawScreen(uint32_t seconds) {
  lcd.clearBuffer();
  lcd.setFont(u8g2_font_6x12_tf);
  // 12 px font cell: baselines 12 px apart keep all text within rows 0..63.
  lcd.drawStr(7, 11, "Inventatory Scanner R1");  // rows 2..14
  lcd.drawHLine(0, 15, kScreenWidth);
  lcd.drawStr(8, 27, "GMG12864");             // rows 18..30
  lcd.drawStr(8, 39, "Display OK");           // rows 30..42
  lcd.drawFrame(0, 44, kScreenWidth, kScreenHeight - 44);  // rows 44..63
  lcd.setCursor(8, 58);                       // rows 49..61
  lcd.print("Up: ");
  lcd.print(seconds);
  lcd.print(" s");
  lcd.sendBuffer();
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("GMG12864-06D SPI test starting");

  lcd.begin();
  // U8g2 maps 0..255 onto the ST7565's 0..63 range; keep low for this panel.
  lcd.setContrast(45);
  drawScreen(0);
}

void loop() {
  static uint32_t lastUpdateMs = 0;
  const uint32_t now = millis();
  if (now - lastUpdateMs >= 1000) {
    lastUpdateMs = now;
    drawScreen(now / 1000);
    Serial.printf("Display update: %lu s\n", static_cast<unsigned long>(now / 1000));
  }
}
