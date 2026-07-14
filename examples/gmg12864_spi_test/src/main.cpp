#include <Arduino.h>
#include <U8g2lib.h>

// GMG12864-06D -> ESP32-S3 GPIO wiring.
constexpr uint8_t kLcdClockPin = 9;   // SCL
constexpr uint8_t kLcdDcPin = 10;     // RS
constexpr uint8_t kLcdResetPin = 11;  // RSE
constexpr uint8_t kLcdCsPin = 12;     // CS
constexpr uint8_t kLcdDataPin = 13;   // SI

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
  lcd.drawStr(8, 16, "Inventatory");
  lcd.drawStr(8, 32, "Scan R1");
  lcd.drawHLine(0, 40, lcd.getDisplayWidth());
  lcd.drawStr(8, 58, "GMG12864");
  lcd.drawStr(8, 74, "Display OK");
  lcd.drawFrame(4, 88, lcd.getDisplayWidth() - 8, 28);
  lcd.setCursor(8, 107);
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
    Serial.printf("Display update: %lu s\\n", static_cast<unsigned long>(now / 1000));
  }
}
