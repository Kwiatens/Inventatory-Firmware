// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#pragma once
#include <Arduino.h>
enum wl_status_t { WL_CONNECTED = 3 };
enum wifi_mode_t { WIFI_OFF = 0, WIFI_STA = 1 };
struct WiFiClient { void setTimeout(unsigned long) {} void setNoDelay(bool) {} bool connect(IPAddress, uint16_t) { return true; } bool connected() { return true; } int available() { return 0; } int read() { return 0; } void stop() {} void print(const char*) {} int printf(const char*, ...) { return 0; } size_t write(const uint8_t*, size_t) { return 0; } };
struct WiFiClass {
  void mode(wifi_mode_t) {} wifi_mode_t getMode() const { return WIFI_STA; } void setAutoConnect(bool) {} void setAutoReconnect(bool) {} void persistent(bool) {}
  void setSleep(bool) {} wl_status_t status() const { return WL_CONNECTED; } bool isConnected() const { return true; }
  bool hostByName(const char*, IPAddress&) { return false; } void begin(const char*, const char*) {} void disconnect(bool) {}
  int RSSI() const { return -50; } IPAddress localIP() const { return IPAddress(); }
};
inline WiFiClass WiFi;
