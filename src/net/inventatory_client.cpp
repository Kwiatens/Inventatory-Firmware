#include "net/inventatory_client.h"

#include "InventatoryScanCore.h"
#include "InventatoryScanSecurity.h"
#include "config/config.h"
#include "config/version.h"
#include "setup/setup_state_store.h"

#include <ESPmDNS.h>
#include <WiFi.h>

namespace inventatory_scan {

namespace {

constexpr unsigned long kHttpResponseTimeoutMs = 600U;

String headerValue(const String& headers, const char* name) {
  const auto wanted = String(name);
  int lineStart = 0;
  while (lineStart < static_cast<int>(headers.length())) {
    const auto lineEnd = headers.indexOf("\r\n", lineStart);
    const auto line = headers.substring(lineStart, lineEnd < 0 ? headers.length() : lineEnd);
    const auto separator = line.indexOf(':');
    if (separator > 0 && line.substring(0, separator).equalsIgnoreCase(wanted)) {
      auto value = line.substring(separator + 1);
      value.trim();
      return value;
    }
    if (lineEnd < 0) break;
    lineStart = lineEnd + 2;
  }
  return {};
}

}  // namespace

bool InventatoryClient::begin(const InventatoryClientConfig& config) {
  config_ = config;
  endpointValid_ = false;
  endpointIp_ = IPAddress(0, 0, 0, 0);
  endpointPort_ = 0;
  endpointName_ = "";
  mdnsStarted_ = false;
  lastResolveAttempt_ = 0;

  if (config_.deviceId.length() == 0 || config_.token.length() == 0 || config_.transportCounterStore == nullptr) {
    return false;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setAutoConnect(false);
  WiFi.setAutoReconnect(false);
  WiFi.persistent(false);
  // Leaving modem sleep disabled raises current draw and can disturb peripherals
  // powered from the same small board/regulator, especially during transmission.
  WiFi.setSleep(true);
  client_.setTimeout(250);
  return true;
}

void InventatoryClient::end() {
  client_.stop();
}

bool InventatoryClient::connected() {
  return WiFi.status() == WL_CONNECTED;
}

bool InventatoryClient::resolveEndpoint(bool force) {
  if (!connected()) {
    endpointValid_ = false;
    return false;
  }

  if (endpointValid_ && !force) {
    return true;
  }

  if (!force && millis() - lastResolveAttempt_ < ENDPOINT_RESOLVE_RETRY_INTERVAL_MS) {
    return false;
  }
  lastResolveAttempt_ = millis();

  if (!mdnsStarted_) {
    mdnsStarted_ = MDNS.begin(config_.deviceId.c_str());
    if (!mdnsStarted_) {
      Serial.println("Inventatory endpoint: failed to start mDNS client");
    }
  }

  const auto count = MDNS.queryService(MDNS_SERVICE, MDNS_PROTOCOL);
  if (count > 0) {
    endpointIp_ = MDNS.IP(0);
    endpointPort_ = MDNS.port(0);
    endpointName_ = MDNS.hostname(0);
    endpointValid_ = endpointPort_ != 0U;
    if (endpointValid_) {
      Serial.print("Inventatory endpoint: discovered ");
      Serial.print(endpointIp_);
      Serial.print(':');
      Serial.println(endpointPort_);
    }
    return endpointValid_;
  }

  if (config_.fallbackPort != 0U && config_.fallbackHost.length() > 0) {
    IPAddress ip;
    if (WiFi.hostByName(config_.fallbackHost.c_str(), ip)) {
      endpointIp_ = ip;
      endpointPort_ = config_.fallbackPort;
      endpointName_ = config_.fallbackHost.c_str();
      endpointValid_ = true;
      Serial.print("Inventatory endpoint: fallback ");
      Serial.print(endpointIp_);
      Serial.print(':');
      Serial.println(endpointPort_);
      return true;
    }
    Serial.print("Inventatory endpoint: could not resolve fallback host ");
    Serial.println(config_.fallbackHost.c_str());
  }

  endpointValid_ = false;
  Serial.print("Inventatory endpoint: no _");
  Serial.print(MDNS_SERVICE);
  Serial.print("._");
  Serial.print(MDNS_PROTOCOL);
  Serial.println(" service found");
  return false;
}

bool InventatoryClient::postDebug(const String& message, const String& level) {
  int httpStatus = 0;
  String responseBody;
  const auto payload = buildDebugRequestJson(config_.deviceId, makeRequestId(config_.deviceId, millis()),
                                             level, message);
  const auto sent = sendJson("/api/device/debug", payload, httpStatus, responseBody);
  if (sent && httpStatus != 200) {
    Serial.print("POST /api/device/debug -> HTTP ");
    Serial.println(httpStatus);
    if (responseBody.length() > 0) {
      Serial.print("Response body: ");
      Serial.println(responseBody.c_str());
    }
  }
  return sent && httpStatus == 200;
}

bool InventatoryClient::connectToResolved() {
  if (!resolveEndpoint()) {
    return false;
  }
  client_.stop();
  if (client_.connect(endpointIp_, endpointPort_)) {
    // Socket options require a live socket; applying this in begin() produces
    // a harmless but alarming EBADF warning on a fresh boot.
    client_.setNoDelay(true);
    return true;
  }
  Serial.print("Inventatory endpoint: TCP connection failed to ");
  Serial.print(endpointIp_);
  Serial.print(':');
  Serial.println(endpointPort_);
  endpointValid_ = false;
  return false;
}

bool InventatoryClient::sendJson(const char* path, const String& body, int& httpStatus, String& responseBody) {
  httpStatus = 0;
  responseBody.clear();

  if (!connectToResolved()) {
    return false;
  }

  uint64_t counter = 0;
  if (!config_.transportCounterStore->reserveTransportCounter(counter)) {
    client_.stop();
    Serial.println("Inventatory endpoint: could not reserve a transport counter");
    return false;
  }
  const auto mac = deviceRequestMac(config_.token, "POST", path, config_.deviceId, counter, body);
  if (mac.length() != 64) {
    client_.stop();
    return false;
  }

  const String hostHeader = endpointName_.length() > 0 ? endpointName_ : endpointIp_.toString();
  client_.printf("POST %s HTTP/1.1\r\n", path);
  client_.printf("Host: %s:%u\r\n", hostHeader.c_str(), endpointPort_);
  client_.printf("User-Agent: Inventatory-Scan-R1/%s\r\n", kFirmwareVersion);
  client_.printf("X-Inventatory-Protocol: %d\r\n", kInventatoryScanProtocolVersion);
  client_.printf("X-Inventatory-Device: %s\r\n", config_.deviceId.c_str());
  client_.printf("X-Inventatory-Counter: %llu\r\n", static_cast<unsigned long long>(counter));
  client_.printf("X-Inventatory-Mac: %s\r\n", mac.c_str());
  client_.printf("Content-Type: application/json\r\n");
  client_.printf("Content-Length: %u\r\n", static_cast<unsigned>(body.length()));
  client_.print("Connection: close\r\n\r\n");
  client_.write(reinterpret_cast<const uint8_t*>(body.c_str()), body.length());

  return readResponse(httpStatus, responseBody, counter);
}

bool InventatoryClient::readResponse(int& httpStatus, String& responseBody, uint64_t counter) {
  String response;
  const auto started = millis();
  while (millis() - started < kHttpResponseTimeoutMs) {
    while (client_.available() > 0) {
      const int raw = client_.read();
      if (raw < 0) {
        break;
      }
      response += static_cast<char>(raw);
    }
    if (!client_.connected() && client_.available() == 0) {
      break;
    }
    delay(1);
  }

  client_.stop();

  const auto text = response.c_str();
  const auto headerEnd = response.indexOf("\r\n\r\n");
  if (headerEnd < 0) {
    return false;
  }
  const auto firstLineEnd = response.indexOf("\r\n");
  const auto statusLine = response.substring(0, firstLineEnd > 0 ? firstLineEnd : response.length());
  if (statusLine.startsWith("HTTP/")) {
    const auto firstSpace = statusLine.indexOf(' ');
    const auto secondSpace = firstSpace < 0 ? -1 : statusLine.indexOf(' ', firstSpace + 1);
    if (firstSpace > 0) {
      const auto statusText = statusLine.substring(firstSpace + 1, secondSpace > firstSpace ? secondSpace : statusLine.length());
      httpStatus = statusText.toInt();
    }
  }
  responseBody = text + headerEnd + 4;
  const auto headers = response.substring(0, headerEnd);
  const auto responseCounter = headerValue(headers, "X-Inventatory-Counter");
  const auto responseMac = headerValue(headers, "X-Inventatory-Mac");
  if (headerValue(headers, "X-Inventatory-Protocol") != String(kInventatoryScanProtocolVersion) ||
      responseCounter != String(counter) ||
      !transportMacMatches(deviceResponseMac(config_.token, counter, httpStatus, responseBody), responseMac)) {
    httpStatus = 0;
    responseBody.clear();
    return false;
  }
  return httpStatus >= 100;
}

bool InventatoryClient::postQuantity(const QuantityRequest& request, int& httpStatus, String& responseBody) {
  const auto payload = buildQuantityRequestJson(request.deviceId, request.requestId, request.code, request.delta);
  return sendJson("/api/device/quantity", payload, httpStatus, responseBody);
}

bool InventatoryClient::postScan(const ScanRequest& request, int& httpStatus, String& responseBody) {
  const auto payload = buildScanRequestJson(request.deviceId, request.requestId, request.code, request.quantity);
  return sendJson("/api/device/scan", payload, httpStatus, responseBody);
}

bool InventatoryClient::postStatus(const String& firmwareVersion, int rssi, const String& debug) {
  int httpStatus = 0;
  String responseBody;
  const auto payload = buildStatusRequestJson(config_.deviceId, firmwareVersion, rssi, debug);
  const auto sent = sendJson("/api/device/status", payload, httpStatus, responseBody);
  if (sent && httpStatus != 200) {
    Serial.print("POST /api/device/status -> HTTP ");
    Serial.println(httpStatus);
    if (responseBody.length() > 0) {
      Serial.print("Response body: ");
      Serial.println(responseBody.c_str());
    }
  }
  return sent && httpStatus == 200;
}

bool InventatoryClient::postSync(const String& payload, int& httpStatus, String& responseBody) {
  return sendJson("/api/v3/device/sync", payload, httpStatus, responseBody);
}

String InventatoryClient::endpointSummary() const {
  if (!endpointValid_) {
    return String("offline");
  }
  return endpointIp_.toString() + ":" + String(endpointPort_);
}

}  // namespace inventatory_scan
