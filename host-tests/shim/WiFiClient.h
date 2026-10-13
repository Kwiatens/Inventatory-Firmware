// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Host stand-in for the ESP32 TCP client. Bytes written by the code under test are
// recorded. The first read gives the request to the test's responder, and the bytes it
// returns are what the "server" sends. The framing rules can then be tested with any
// byte sequence, including malformed ones.
#pragma once

#include "Arduino.h"
#include "hostnet.h"

#include <cstdarg>
#include <cstdio>

class WiFiClient {
 public:
  void setTimeout(unsigned long) {}
  void setNoDelay(bool) {}

  bool connect(IPAddress, uint16_t) {
    HostConnection& net = hostNetwork();
    net.request.clear();
    net.response.clear();
    net.readPos = 0;
    net.responded = false;
    return net.connectSucceeds;
  }

  bool connected() {
    const HostConnection& net = hostNetwork();
    // The server closes after its response, as with "Connection: close".
    return !(net.responded && net.readPos >= net.response.size());
  }

  int available() {
    HostConnection& net = hostNetwork();
    if (!net.responded && net.responder) {
      net.responded = true;
      net.response = net.responder(net.request);
    }
    return static_cast<int>(net.response.size() - net.readPos);
  }

  int read() {
    HostConnection& net = hostNetwork();
    if (net.readPos >= net.response.size()) return -1;
    return static_cast<unsigned char>(net.response[net.readPos++]);
  }

  void stop() {}

  size_t write(const uint8_t* data, size_t length) {
    hostNetwork().request.append(reinterpret_cast<const char*>(data), length);
    return length;
  }

  size_t print(const char* text) {
    hostNetwork().request += text;
    return std::char_traits<char>::length(text);
  }

  size_t printf(const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    const int written = std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if (written <= 0) return 0;
    hostNetwork().request.append(buffer, static_cast<std::size_t>(written));
    return static_cast<std::size_t>(written);
  }
};
