// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#include "net/inventatory_client.h"

#include "InventatoryScanCore.h"
#include "InventatoryScanHttp.h"
#include "InventatoryScanSecurity.h"
#include "config/config.h"
#include "config/version.h"
#include "setup/setup_state_store.h"
#include "log/device_log.h"

#include <ESPmDNS.h>
#include <WiFi.h>

namespace inventatory_scan {

namespace {

constexpr unsigned long kHttpResponseTimeoutMs = 600U;
// A firmware chunk is up to 8 KiB, and the desktop may read it from disk first.
constexpr unsigned long kFirmwareResponseTimeoutMs = 3000U;
// Connect and send timeout, in milliseconds. WiFiClient::setTimeout() takes seconds on this core, so the
// old setTimeout(250) let a host that drops SYNs hold the network worker for minutes.
constexpr int32_t kTcpConnectTimeoutMs = 1500;
// Any host can answer mDNS. After this many exchanges in a row without an authenticated response the
// endpoint is dropped and mDNS is asked again, so a host that won one answer does not keep the device.
constexpr uint8_t kMaxFailedExchangesPerEndpoint = 3;
// Reads a header that may appear at most once; a repeat is refused. An absent header leaves
// the value empty and still succeeds.
bool uniqueHeaderValue(const String& headers, const char* name, String& value) {
  const auto wanted = String(name);
  bool found = false;
  value = String();
  int lineStart = 0;
  while (lineStart < static_cast<int>(headers.length())) {
    const auto lineEnd = headers.indexOf("\r\n", lineStart);
    const auto line = headers.substring(lineStart, lineEnd < 0 ? headers.length() : lineEnd);
    const auto separator = line.indexOf(':');
    if (separator > 0 && line.substring(0, separator).equalsIgnoreCase(wanted)) {
      if (found) return false;
      found = true;
      value = line.substring(separator + 1);
      value.trim();
    }
    if (lineEnd < 0) break;
    lineStart = lineEnd + 2;
  }
  return true;
}

bool parseContentLength(const String& value, std::size_t& length) {
  if (value.length() == 0) return false;
  std::size_t parsed = 0;
  for (std::size_t index = 0; index < value.length(); ++index) {
    const char ch = value[index];
    if (ch < '0' || ch > '9') return false;
    const auto digit = static_cast<std::size_t>(ch - '0');
    if (parsed > (kMaxHttpBodyBytes - digit) / 10U) return false;
    parsed = parsed * 10U + digit;
  }
  length = parsed;
  return true;
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
  failedExchanges_ = 0;

  if (config_.deviceId.length() == 0 || !isValidTransportToken(config_.token) ||
      config_.transportCounterStore == nullptr) {
    return false;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setAutoConnect(false);
  WiFi.setAutoReconnect(false);
  WiFi.persistent(false);
  // Modem sleep stays on, since Wi-Fi current draw can disturb peripherals on the same regulator.
  WiFi.setSleep(true);
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
      deviceLog.println("Inventatory endpoint: failed to start mDNS client");
    }
  }

  const auto count = MDNS.queryService(MDNS_SERVICE, MDNS_PROTOCOL);
  if (count > 0) {
    endpointIp_ = MDNS.IP(0);
    endpointPort_ = MDNS.port(0);
    endpointName_ = MDNS.hostname(0);
    // An unusable name falls back to the address in the Host header.
    if (!isSafeHttpHostName(endpointName_.c_str(), endpointName_.length())) endpointName_ = "";
    endpointValid_ = endpointPort_ != 0U;
    if (endpointValid_) {
      deviceLog.print("Inventatory endpoint: discovered ");
      deviceLog.print(endpointIp_);
      deviceLog.print(':');
      deviceLog.println(endpointPort_);
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
      deviceLog.print("Inventatory endpoint: fallback ");
      deviceLog.print(endpointIp_);
      deviceLog.print(':');
      deviceLog.println(endpointPort_);
      return true;
    }
    deviceLog.print("Inventatory endpoint: could not resolve fallback host ");
    deviceLog.println(config_.fallbackHost.c_str());
  }

  endpointValid_ = false;
  deviceLog.print("Inventatory endpoint: no _");
  deviceLog.print(MDNS_SERVICE);
  deviceLog.print("._");
  deviceLog.print(MDNS_PROTOCOL);
  deviceLog.println(" service found");
  return false;
}

bool InventatoryClient::connectToResolved() {
  if (!resolveEndpoint()) {
    lastFailure_ = "no endpoint";
    return false;
  }
  client_.stop();
  if (client_.connect(endpointIp_, endpointPort_, kTcpConnectTimeoutMs)) {
    // Needs a live socket; setting it in begin() logs a harmless EBADF warning on a fresh boot.
    client_.setNoDelay(true);
    return true;
  }
  lastFailure_ = "TCP connect failed";
  deviceLog.print("Inventatory endpoint: TCP connection failed to ");
  deviceLog.print(endpointIp_);
  deviceLog.print(':');
  deviceLog.println(endpointPort_);
  endpointValid_ = false;
  failedExchanges_ = 0;
  return false;
}

bool InventatoryClient::sendJson(const char* path, const String& body, int& httpStatus, String& responseBody,
                                 unsigned long responseTimeoutMs) {
  httpStatus = 0;
  responseBody.clear();
  lastFailure_ = "";

  if (!connectToResolved()) {
    return false;
  }

  uint64_t counter = 0;
  if (!config_.transportCounterStore->reserveTransportCounter(counter)) {
    client_.stop();
    lastFailure_ = "no transport counter";
    deviceLog.println("Inventatory endpoint: could not reserve a transport counter");
    return false;
  }
  const auto mac = deviceRequestMac(config_.token, "POST", path, config_.deviceId, counter, body);
  if (mac.length() != 64) {
    client_.stop();
    lastFailure_ = "no request MAC";
    return false;
  }

  const String hostHeader = endpointName_.length() > 0 ? endpointName_ : endpointIp_.toString();
  client_.printf("POST %s HTTP/1.1\r\n", path);
  client_.printf("Host: %s:%u\r\n", hostHeader.c_str(), endpointPort_);
  client_.printf("User-Agent: Inventatory-Scanner-R1/%s\r\n", kFirmwareVersion);
  client_.printf("X-Inventatory-Protocol: %d\r\n", kInventatoryScanProtocolVersion);
  client_.printf("X-Inventatory-Device: %s\r\n", config_.deviceId.c_str());
  client_.printf("X-Inventatory-Counter: %llu\r\n", static_cast<unsigned long long>(counter));
  client_.printf("X-Inventatory-Mac: %s\r\n", mac.c_str());
  client_.printf("Content-Type: application/json\r\n");
  client_.printf("Content-Length: %u\r\n", static_cast<unsigned>(body.length()));
  client_.print("Connection: close\r\n\r\n");
  // A short write leaves the server waiting for the rest of the body, so no reply can come. Stop here.
  const auto bodyLength = body.length();
  const bool bodySent = client_.write(reinterpret_cast<const uint8_t*>(body.c_str()), bodyLength) == bodyLength;
  if (!bodySent) {
    client_.stop();
    lastFailure_ = "request cut short";
  }
  const bool authenticated = bodySent && readResponse(httpStatus, responseBody, counter, responseTimeoutMs);
  if (authenticated) {
    failedExchanges_ = 0;
  } else if (++failedExchanges_ >= kMaxFailedExchangesPerEndpoint) {
    deviceLog.println("Inventatory endpoint: no authenticated response; asking mDNS again");
    endpointValid_ = false;
    failedExchanges_ = 0;
  }
  return authenticated;
}

bool InventatoryClient::readResponse(int& httpStatus, String& responseBody, uint64_t counter,
                                     unsigned long timeoutMs) {
  httpStatus = 0;
  responseBody.clear();
  String response;
  // Reserve the largest permitted response once, rather than growing it while the body arrives.
  response.reserve(kMaxHttpResponseBytes);
  int headerEnd = -1;
  const auto started = millis();
  while (millis() - started < timeoutMs) {
    while (client_.available() > 0) {
      const int raw = client_.read();
      if (raw < 0) {
        break;
      }
      if (!httpResponseAppendAllowed(response.length())) {
        client_.stop();
        lastFailure_ = "response too large";
        return false;
      }
      response += static_cast<char>(raw);
      if (headerEnd < 0) {
        // NUL is refused in the header: C-string calls stop there and could hide a later header line.
        if (raw == 0) {
          client_.stop();
          lastFailure_ = "NUL in header";
          return false;
        }
        // The header ends at the first CR LF CR LF; only the newest four bytes need checking.
        const auto length = response.length();
        if (length >= 4U && response[length - 1U] == '\n' && response[length - 2U] == '\r' &&
            response[length - 3U] == '\n' && response[length - 4U] == '\r') {
          headerEnd = static_cast<int>(length - 4U);
        } else if (length > kMaxHttpHeaderBytes + 4U) {
          // A header may be kMaxHttpHeaderBytes plus the blank line; a longer one is refused early.
          // The exact size is checked again below.
          client_.stop();
          lastFailure_ = "header too large";
          return false;
        }
      }
    }
    if (!client_.connected() && client_.available() == 0) {
      break;
    }
    delay(1);
  }

  client_.stop();
  const bool timedOut = millis() - started >= timeoutMs;

  if (headerEnd < 0) {
    lastFailure_ = timedOut ? "no answer in time" : "connection closed before the header";
    return false;
  }
  if (!httpHeaderWithinLimit(static_cast<std::size_t>(headerEnd))) {
    lastFailure_ = "header too large";
    return false;
  }
  const auto firstLineEnd = response.indexOf("\r\n");
  const auto statusLine = response.substring(0, firstLineEnd > 0 ? firstLineEnd : response.length());
  // The status line is unauthenticated until the MAC check passes; only then does it reach httpStatus.
  int parsedStatus = 0;
  if (statusLine.startsWith("HTTP/")) {
    const auto firstSpace = statusLine.indexOf(' ');
    const auto secondSpace = firstSpace < 0 ? -1 : statusLine.indexOf(' ', firstSpace + 1);
    if (firstSpace > 0) {
      const auto statusText = statusLine.substring(firstSpace + 1, secondSpace > firstSpace ? secondSpace : statusLine.length());
      parsedStatus = statusText.toInt();
    }
  }
  const auto headers = response.substring(0, headerEnd);
  String transferEncoding;
  String contentLengthText;
  String responseCounter;
  String responseMac;
  String protocolVersion;
  if (!uniqueHeaderValue(headers, "Transfer-Encoding", transferEncoding) || transferEncoding.length() > 0 ||
      !uniqueHeaderValue(headers, "Content-Length", contentLengthText) ||
      !uniqueHeaderValue(headers, "X-Inventatory-Counter", responseCounter) ||
      !uniqueHeaderValue(headers, "X-Inventatory-Mac", responseMac) ||
      !uniqueHeaderValue(headers, "X-Inventatory-Protocol", protocolVersion)) {
    lastFailure_ = "bad headers";
    return false;
  }
  std::size_t contentLength = 0;
  if (!parseContentLength(contentLengthText, contentLength) ||
      !httpBodyWithinLimit(contentLength) ||
      response.length() != static_cast<std::size_t>(headerEnd + 4) + contentLength) {
    lastFailure_ = timedOut ? "no answer in time" : "body cut short or too long";
    return false;
  }
  // Copy by declared length: a C-string copy would stop at NUL and authenticate a truncated body.
  responseBody.reserve(contentLength);
  for (std::size_t index = 0; index < contentLength; ++index) {
    responseBody += response[static_cast<unsigned int>(headerEnd + 4) + static_cast<unsigned int>(index)];
  }
  if (protocolVersion != String(kInventatoryScanProtocolVersion) ||
      responseCounter != String(counter) ||
      !transportMacMatches(deviceResponseMac(config_.token, counter, parsedStatus, responseBody), responseMac)) {
    httpStatus = 0;
    responseBody.clear();
    lastFailure_ = "reply MAC or counter wrong";
    return false;
  }
  if (parsedStatus < 100) {
    responseBody.clear();
    lastFailure_ = "bad status line";
    return false;
  }
  httpStatus = parsedStatus;
  return true;
}

bool InventatoryClient::postSync(const String& payload, int& httpStatus, String& responseBody) {
  return sendJson("/api/v1/device/sync", payload, httpStatus, responseBody, kHttpResponseTimeoutMs);
}

bool InventatoryClient::postFirmware(const char* path, const String& body, int& httpStatus, String& responseBody) {
  return sendJson(path, body, httpStatus, responseBody, kFirmwareResponseTimeoutMs);
}

}  // namespace inventatory_scan
