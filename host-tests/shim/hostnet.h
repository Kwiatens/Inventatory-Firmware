// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Network state shared by the host WiFi, mDNS and TCP stand-ins. Tests set conditions here.
#pragma once

#include "Arduino.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

struct HostConnection {
  bool wifiConnected = true;
  bool mdnsFindsServer = true;
  bool connectSucceeds = true;
  IPAddress serverAddress{192, 168, 1, 20};
  uint16_t serverPort = 8787;
  std::string request;   // bytes written by the code under test on the current connection
  std::string response;  // bytes the server sends, from the responder
  std::size_t readPos = 0;
  bool responded = false;
  // Called on the first read with the request; returns the response. Unset: server stays silent.
  std::function<std::string(const std::string& request)> responder;
};

inline HostConnection& hostNetwork() {
  static HostConnection state;
  return state;
}
