// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// State shared by the host stand-ins for WiFi, mDNS and the TCP client. The tests set
// the network conditions here and read back the bytes the code under test sent.
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
  std::string request;   // bytes the code under test wrote on the current connection
  std::string response;  // bytes the server sends back, produced by the responder
  std::size_t readPos = 0;
  bool responded = false;
  // Called with the request the first time the code reads. Returns the response bytes.
  // With no responder the server stays silent, which exercises the response timeout.
  std::function<std::string(const std::string& request)> responder;
};

inline HostConnection& hostNetwork() {
  static HostConnection state;
  return state;
}
