#include <Arduino.h>

#include "app/hims_scan_app.h"

using namespace hims_scan;

static HimsScanApp app;

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
  Serial.println("HIMS Scan R1 booting");
  app.begin();
}

void loop() {
  app.loop();
}
