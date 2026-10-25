// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Copy to config.h and fill in real values. config.h is private and git-ignored.

// Letters, digits and '-' only, up to 63 characters. Give each scanner its own id: it names the device on
// the network (mDNS), in Bluetooth setup and in its event ids.
const char DEVICE_ID[] = "Inventatory-SCANNER-R1";
// Updates over Wi-Fi need no setting here: they are signed, and the trusted keys are in ota_keys.h.

// Used only if mDNS discovery fails.
const char FALLBACK_HOST[] = "";
const int FALLBACK_PORT = 0;

// Every keypad line must be an RTC GPIO (0-21). '*' wakes from deep sleep on COLUMN_1 while ROW_4
// is held low. Keep GPIO0, GPIO3, GPIO45, GPIO46 (boot straps, RGB LED) and GPIO48 (SCK LED) free.
const int KEYPAD_COLUMN_1_PIN = 5;   // D2
const int KEYPAD_COLUMN_2_PIN = 6;   // D3
const int KEYPAD_COLUMN_3_PIN = 7;   // D4
const int KEYPAD_COLUMN_4_PIN = 8;   // D5

const int KEYPAD_ROW_1_PIN = 9;      // D6
const int KEYPAD_ROW_2_PIN = 10;     // D7
const int KEYPAD_ROW_3_PIN = 17;     // D8
const int KEYPAD_ROW_4_PIN = 18;     // D9

// LiPo sense: BAT+ -> TOP_OHMS -> BATTERY_ADC_PIN -> BOTTOM_OHMS -> GND, with 100 nF to GND.
// Use an ADC1 pin: ADC2 cannot be read while Wi-Fi runs.
// Equal resistors put 4.2 V on the pin as 2.1 V.
const int BATTERY_ADC_PIN = 1;       // A0
const unsigned long BATTERY_DIVIDER_TOP_OHMS = 100000UL;
const unsigned long BATTERY_DIVIDER_BOTTOM_OHMS = 100000UL;

// Named from the ESP32's side: TX drives GM65 RX, RX reads GM65 TX (Nano D1/D0).
// The ESP32 is 3.3 V only: confirm the GM65 TX level first.
const int GM65_TX_PIN = 43;
const int GM65_RX_PIN = 44;

const char MDNS_SERVICE[] = "inventatory";
const char MDNS_PROTOCOL[] = "tcp";

const unsigned long WIFI_RECONNECT_INTERVAL_MS = 15000;
const unsigned long ENDPOINT_RESOLVE_RETRY_INTERVAL_MS = 5000;
const unsigned long FLUSH_INTERVAL_MS = 300;
// Keep well under the terminal's 15 s offline timeout.
const unsigned long STATUS_INTERVAL_MS = 5000;
