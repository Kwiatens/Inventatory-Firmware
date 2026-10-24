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
  // A firmware update request (docs/ota-protocol.md): the same authenticated exchange as sync, with a
  // longer wait, since a chunk response carries up to 8 KiB. The body may hold any byte, NUL included.
  bool postFirmware(const char* path, const String& body, int& httpStatus, String& responseBody);
  // Why the last exchange gave no authenticated answer, for the device log. Empty after an answer.
  const char* lastFailure() const { return lastFailure_; }

 private:
  bool connectToResolved();
  bool sendJson(const char* path, const String& body, int& httpStatus, String& responseBody,
                unsigned long responseTimeoutMs);
  bool readResponse(int& httpStatus, String& responseBody, uint64_t counter, unsigned long timeoutMs);

  InventatoryClientConfig config_;
  WiFiClient client_;
  IPAddress endpointIp_{0, 0, 0, 0};
  uint16_t endpointPort_ = 0;
  String endpointName_;
  bool endpointValid_ = false;
  bool mdnsStarted_ = false;
  unsigned long lastResolveAttempt_ = 0;
  // Exchanges in a row with the current endpoint that gave no authenticated response.
  uint8_t failedExchanges_ = 0;
  const char* lastFailure_ = "";
};

}  // namespace inventatory_scan
