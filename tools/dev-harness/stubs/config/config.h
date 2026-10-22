// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Copy to config.h and fill in real values. config.h is git-ignored.

const char DEVICE_ID[] = "Inventatory-SCAN-R1";
const char OTA_PASSWORD[] = "paste-your-ota-password-here";

// Used only if mDNS discovery fails.
const char FALLBACK_HOST[] = "";
const int FALLBACK_PORT = 0;

// Keypad lines must be RTC GPIOs (0-21): '*' wakes from deep sleep via KEYPAD_COLUMN_1_PIN
// while KEYPAD_ROW_4_PIN is held low. Avoid GPIO0, 3, 45, 46 (boot straps, RGB LED) and GPIO48.
const int KEYPAD_COLUMN_1_PIN = 5;   // D2
const int KEYPAD_COLUMN_2_PIN = 6;   // D3
const int KEYPAD_COLUMN_3_PIN = 7;   // D4
const int KEYPAD_COLUMN_4_PIN = 8;   // D5

const int KEYPAD_ROW_1_PIN = 9;      // D6
const int KEYPAD_ROW_2_PIN = 10;     // D7
const int KEYPAD_ROW_3_PIN = 17;     // D8
const int KEYPAD_ROW_4_PIN = 18;     // D9

// LiPo divider midpoint, equal resistors (4.2 V cell reads as 2.1 V). Use an ADC1 pin (GPIO1-10):
// ADC2 cannot be read while Wi-Fi runs.
const int BATTERY_ADC_PIN = 1;       // A0
const unsigned long BATTERY_DIVIDER_TOP_OHMS = 100000UL;
const unsigned long BATTERY_DIVIDER_BOTTOM_OHMS = 100000UL;

// GM65 pins are named from the ESP32 side (TX drives GM65 RX). Nano TXD/RXD headers D1/D0.
// The ESP32 is 3.3 V only: confirm the GM65 TX level first.
const int GM65_TX_PIN = 43;
const int GM65_RX_PIN = 44;

const char MDNS_SERVICE[] = "inventatory";
const char MDNS_PROTOCOL[] = "tcp";

const unsigned long WIFI_RECONNECT_INTERVAL_MS = 15000;
const unsigned long ENDPOINT_RESOLVE_RETRY_INTERVAL_MS = 5000;
const unsigned long FLUSH_INTERVAL_MS = 300;
// Terminal marks the scanner offline after 15 s without a heartbeat.
const unsigned long STATUS_INTERVAL_MS = 5000;
