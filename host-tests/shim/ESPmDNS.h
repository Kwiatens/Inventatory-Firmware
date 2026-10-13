// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Host stand-in for the ESP32 mDNS client. The tests decide whether a query finds the server.
#pragma once

#include "Arduino.h"
#include "hostnet.h"

class MDNSClass {
 public:
  bool begin(const char*) { return true; }
  int queryService(const char*, const char*) { return hostNetwork().mdnsFindsServer ? 1 : 0; }
  IPAddress IP(int) { return hostNetwork().serverAddress; }
  uint16_t port(int) { return hostNetwork().serverPort; }
  String hostname(int) { return String("inventatory.local"); }
};

inline MDNSClass MDNS;
