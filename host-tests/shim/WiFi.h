// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Host stand-in for the ESP32 WiFi API: only the calls the network client makes.
#pragma once

#include "Arduino.h"
#include "WiFiClient.h"
#include "hostnet.h"

enum wl_status_t { WL_IDLE_STATUS = 0, WL_CONNECTED = 3, WL_DISCONNECTED = 6 };
enum wifi_mode_t { WIFI_OFF = 0, WIFI_STA = 1 };

class WiFiClass {
 public:
  void mode(wifi_mode_t mode) { mode_ = mode; }
  wifi_mode_t getMode() const { return mode_; }
  void setAutoConnect(bool) {}
  void setAutoReconnect(bool) {}
  void persistent(bool) {}
  void setSleep(bool) {}
  wl_status_t status() const { return hostNetwork().wifiConnected ? WL_CONNECTED : WL_DISCONNECTED; }
  bool isConnected() const { return status() == WL_CONNECTED; }
  bool hostByName(const char*, IPAddress&) { return false; }

 private:
  wifi_mode_t mode_ = WIFI_OFF;
};

inline WiFiClass WiFi;
