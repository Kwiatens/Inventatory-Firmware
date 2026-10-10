// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "InventatoryScanCore.h"
#include "storage/outbox_store.h"

#include <Arduino.h>
#include <WiFi.h>

namespace inventatory_scan {

class SetupStateStore;

struct InventatoryClientConfig {
  String deviceId;
  String token;
  SetupStateStore* transportCounterStore = nullptr;
  String fallbackHost;
  uint16_t fallbackPort = 0;
};

class InventatoryClient {
 public:
  bool begin(const InventatoryClientConfig& config);
  void end();

  bool connected();
  bool resolveEndpoint(bool force = false);
  bool postSync(const String& payload, int& httpStatus, String& responseBody);

 private:
  bool connectToResolved();
  bool sendJson(const char* path, const String& body, int& httpStatus, String& responseBody);
  bool readResponse(int& httpStatus, String& responseBody, uint64_t counter);

  InventatoryClientConfig config_;
  WiFiClient client_;
  IPAddress endpointIp_{0, 0, 0, 0};
  uint16_t endpointPort_ = 0;
  String endpointName_;
  bool endpointValid_ = false;
  bool mdnsStarted_ = false;
  unsigned long lastResolveAttempt_ = 0;
};

}  // namespace inventatory_scan
