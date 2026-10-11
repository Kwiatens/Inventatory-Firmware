// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Copy this file to config.h and fill in your real values.
// config.h is private and ignored by git.

const char DEVICE_ID[] = "Inventatory-SCAN-R1";
const char OTA_PASSWORD[] = "paste-your-ota-password-here";

// Optional fallback if mDNS discovery fails.
const char FALLBACK_HOST[] = "";
const int FALLBACK_PORT = 0;

// Keypad matrix wiring, Waveshare ESP32-S3-Nano header labels in the comments.
// Every keypad line must be an RTC GPIO (0-21): the '*' key wakes the chip from deep sleep through
// KEYPAD_COLUMN_1_PIN while KEYPAD_ROW_4_PIN is held low. Avoid GPIO0, GPIO3, GPIO45 and GPIO46
// (boot straps and the RGB LED) and GPIO48 (drives the board's SCK LED).
const int KEYPAD_COLUMN_1_PIN = 5;   // D2
const int KEYPAD_COLUMN_2_PIN = 6;   // D3
const int KEYPAD_COLUMN_3_PIN = 7;   // D4
const int KEYPAD_COLUMN_4_PIN = 8;   // D5

const int KEYPAD_ROW_1_PIN = 9;      // D6
const int KEYPAD_ROW_2_PIN = 10;     // D7
const int KEYPAD_ROW_3_PIN = 17;     // D8
const int KEYPAD_ROW_4_PIN = 18;     // D9

// LiPo voltage sense: BAT+ -> BATTERY_DIVIDER_TOP_OHMS -> BATTERY_ADC_PIN -> BATTERY_DIVIDER_BOTTOM_OHMS -> GND,
// with 100 nF from BATTERY_ADC_PIN to GND. Use an ADC1 pin (GPIO1-10): ADC2 cannot be read while Wi-Fi runs.
// Equal 100 kOhm resistors put 4.2 V on the pin as 2.1 V.
const int BATTERY_ADC_PIN = 1;       // A0
const unsigned long BATTERY_DIVIDER_TOP_OHMS = 100000UL;
const unsigned long BATTERY_DIVIDER_BOTTOM_OHMS = 100000UL;

// GM65 barcode scanner wiring, named from the ESP32's side:
// GM65_TX_PIN is the ESP32 transmit pin (to GM65 RX), GM65_RX_PIN the ESP32 receive pin (from GM65 TX).
// The Nano's TXD/RXD header pins (D1/D0). The ESP32 is 3.3 V only: confirm the GM65 TX level first.
const int GM65_TX_PIN = 43;
const int GM65_RX_PIN = 44;

const char MDNS_SERVICE[] = "inventatory";
const char MDNS_PROTOCOL[] = "tcp";

const unsigned long WIFI_RECONNECT_INTERVAL_MS = 15000;
const unsigned long ENDPOINT_RESOLVE_RETRY_INTERVAL_MS = 5000;
const unsigned long FLUSH_INTERVAL_MS = 300;
// The terminal marks the scanner offline after 15 seconds without a heartbeat.
const unsigned long STATUS_INTERVAL_MS = 5000;
