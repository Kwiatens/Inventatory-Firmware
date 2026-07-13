#pragma once

// Copy this file to config.h and fill in your real values.
// config.h is private and ignored by git.

const char WIFI_SSID[] = "your-wifi-name";
const char WIFI_PASSWORD[] = "your-wifi-password";
const bool WIFI_AUTOSTART = true;

const char DEVICE_ID[] = "HIMS-SCAN-R1";
const char DEVICE_TOKEN[] = "paste-your-hims-pairing-token-here";
const char OTA_PASSWORD[] = "paste-your-ota-password-here";

// Optional fallback if mDNS discovery fails.
const char FALLBACK_HOST[] = "";
const int FALLBACK_PORT = 0;

// Keypad matrix wiring.
const int KEYPAD_COLUMN_1_PIN = 0;
const int KEYPAD_COLUMN_2_PIN = 1;
const int KEYPAD_COLUMN_3_PIN = 7;
const int KEYPAD_COLUMN_4_PIN = 6;

const int KEYPAD_ROW_1_PIN = 4;
const int KEYPAD_ROW_2_PIN = 5;
const int KEYPAD_ROW_3_PIN = 3;
const int KEYPAD_ROW_4_PIN = 2;

// GM65 barcode scanner wiring.
const int GM65_TX_PIN = 43;
const int GM65_RX_PIN = 44;

const char MDNS_SERVICE[] = "hims";
const char MDNS_PROTOCOL[] = "tcp";

const unsigned long WIFI_RECONNECT_INTERVAL_MS = 15000;
const unsigned long ENDPOINT_RESOLVE_RETRY_INTERVAL_MS = 5000;
const unsigned long FLUSH_INTERVAL_MS = 300;
// The terminal marks the scanner offline after 15 seconds without a heartbeat.
const unsigned long STATUS_INTERVAL_MS = 5000;
