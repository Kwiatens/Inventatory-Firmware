#include <Arduino.h>
#include <esp_system.h>

#include "app/inventatory_scan_app.h"

using namespace inventatory_scan;

static InventatoryScanApp app;

void setup() {
  Serial.begin(115200);
  const unsigned long start = millis();
#if ARDUINO_USB_CDC_ON_BOOT
  while (!Serial && millis() - start < 3000UL) {
    delay(10);
  }
#else
  delay(1000); // Wait for Serial to initialize
#endif
  Serial.println("Inventatory Scan R1 booting");
  Serial.printf("Reset reason: %d\r\n", static_cast<int>(esp_reset_reason()));
  app.begin();
}

void loop() {
  app.loop();
}
