// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#include <Arduino.h>
#include <U8g2lib.h>

// GMG12864-06D -> ESP32-S3-Nano GPIO wiring (Waveshare header labels in the comments).
constexpr uint8_t kLcdClockPin = 13;  // SCL, A6
constexpr uint8_t kLcdDcPin = 11;     // RS, A4
constexpr uint8_t kLcdResetPin = 12;  // RSE, A5
constexpr uint8_t kLcdCsPin = 21;     // CS, D10
constexpr uint8_t kLcdDataPin = 14;   // SI, A7

// The ALT variant uses the GMG12864-safe LCD bias and low contrast settings;
// the standard ST7565 configuration can turn the entire panel dark.
// Software SPI intentionally uses the exact GPIOs above.
U8G2_ST7565_ERC12864_ALT_F_4W_SW_SPI lcd(
    U8G2_R1,
    /* clock=*/kLcdClockPin,
    /* data=*/kLcdDataPin,
    /* cs=*/kLcdCsPin,
    /* dc=*/kLcdDcPin,
    /* reset=*/kLcdResetPin);

void drawScreen(uint32_t seconds) {
  lcd.clearBuffer();
  lcd.setFont(u8g2_font_6x12_tf);
  // u8g2_font_6x12_tf is a 12 px cell (about 9 px ascent, 3 px descent), so each
  // baseline sits 12 px below the previous one and everything stays in 0..63.
  lcd.drawStr(7, 11, "Inventatory Scan R1");  // rows 2..14
  lcd.drawHLine(0, 15, lcd.getDisplayWidth());
  lcd.drawStr(8, 27, "GMG12864");             // rows 18..30
  lcd.drawStr(8, 39, "Display OK");           // rows 30..42
  lcd.drawFrame(0, 44, lcd.getDisplayWidth(), 20);  // rows 44..63
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
  // U8g2 maps 0..255 to the ST7565's 0..63 range. Keep this panel low.
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
