// HIMS Scan R1 application controller.

#include "app/hims_scan_app.h"

#include <ArduinoOTA.h>

#include "HimsScanCore.h"
#include "config/config.h"
#include "config/version.h"

#include <cctype>
#include <cstring>
#include <WiFi.h>

namespace hims_scan {

namespace {

bool isQuantityCode(const String& code) {
  const auto trimmed = trimCopy(code);
  if (trimmed.length() == 0) {
    return false;
  }
  for (size_t index = 0; index < trimmed.length(); ++index) {
    if (!std::isdigit(static_cast<unsigned char>(trimmed[index]))) {
      return false;
    }
  }
  return true;
}

bool hasAimPrefix(const String& value, char symbology) {
  return value.length() >= 2U && value[0] == ']' &&
         std::tolower(static_cast<unsigned char>(value[1])) == std::tolower(static_cast<unsigned char>(symbology));
}

bool hasIso15434Macro06Envelope(const String& value) {
  if (!value.startsWith("[)>")) {
    return false;
  }

  if (value.length() >= 6U && static_cast<unsigned char>(value[3]) == 0x1EU && value[4] == '0' && value[5] == '6') {
    return true;
  }

  // Some scanner profiles suppress RS/GS control characters. A DigiKey Macro
  // 06 payload can then arrive as "[)>06P..." rather than "[)><RS>06<GS>P...".
  return value.length() >= 5U && value[3] == '0' && value[4] == '6';
}

bool isIso15434Separator(char ch) {
  const auto byte = static_cast<unsigned char>(ch);
  return byte == 0x1DU || byte == 0x1EU || byte == 0x04U;
}

size_t iso15434PayloadOffset(const String& value) {
  if (value.length() >= 7U && static_cast<unsigned char>(value[3]) == 0x1EU && value[4] == '0' &&
      value[5] == '6' && static_cast<unsigned char>(value[6]) == 0x1DU) {
    return 7U;
  }
  if (value.length() >= 5U && value[3] == '0' && value[4] == '6') {
    return 5U;
  }
  return 0U;
}

bool fieldStartsWithDataIdentifier(const String& field, const char* identifier) {
  const auto idLength = strlen(identifier);
  return field.length() > idLength && field.substring(0, idLength) == identifier;
}

String findSeparatedDataIdentifier(const String& value, const char* identifier) {
  auto index = iso15434PayloadOffset(value);
  while (index < value.length()) {
    while (index < value.length() && isIso15434Separator(value[index])) {
      ++index;
    }
    const auto fieldStart = index;
    while (index < value.length() && !isIso15434Separator(value[index])) {
      ++index;
    }
    const auto field = value.substring(fieldStart, index);
    if (fieldStartsWithDataIdentifier(field, identifier)) {
      return field.substring(strlen(identifier));
    }
  }
  return {};
}

String findSuppressedSeparatorDigikeyPart(const String& value) {
  const auto offset = iso15434PayloadOffset(value);
  if (offset >= value.length() || value[offset] != 'P') {
    return {};
  }

  const auto partStart = offset + 1U;
  auto partEnd = value.indexOf("1P", partStart);
  if (partEnd < 0) {
    partEnd = value.indexOf("30P", partStart);
  }
  if (partEnd < 0) {
    return {};
  }
  return value.substring(partStart, partEnd);
}

String findSuppressedQuantityField(const String& value) {
  const auto offset = iso15434PayloadOffset(value);
  for (size_t index = offset; index < value.length(); ++index) {
    if (value[index] != 'Q') {
      continue;
    }
    const auto next = index + 1U;
    if (next >= value.length() || !std::isdigit(static_cast<unsigned char>(value[next]))) {
      continue;
    }
    size_t end = next;
    while (end < value.length() && std::isdigit(static_cast<unsigned char>(value[end]))) {
      ++end;
    }
    return value.substring(next, end);
  }
  return {};
}

String digikeyLookupCodeFromDataMatrix(const String& value) {
  auto lookup = findSeparatedDataIdentifier(value, "P");
  if (lookup.length() == 0) {
    lookup = findSeparatedDataIdentifier(value, "30P");
  }
  if (lookup.length() == 0) {
    lookup = findSuppressedSeparatorDigikeyPart(value);
  }
  if (lookup.length() == 0) {
    lookup = findSeparatedDataIdentifier(value, "1P");
  }
  return trimCopy(lookup);
}

int digikeyQuantityFromDataMatrix(const String& value) {
  auto quantity = findSeparatedDataIdentifier(value, "Q");
  if (quantity.length() == 0) {
    quantity = findSuppressedQuantityField(value);
  }
  const auto trimmed = trimCopy(quantity);
  if (trimmed.length() == 0) {
    return 1;
  }
  for (size_t index = 0; index < trimmed.length(); ++index) {
    if (!std::isdigit(static_cast<unsigned char>(trimmed[index]))) {
      return 1;
    }
  }
  const long parsed = trimmed.toInt();
  return parsed <= 0 ? 1 : static_cast<int>(parsed);
}

bool isDataMatrixScan(const String& value) {
  return hasAimPrefix(value, 'd') || hasIso15434Macro06Envelope(stripAimSymbologyPrefix(value));
}

bool isQrScan(const String& value) {
  return hasAimPrefix(value, 'q');
}

}  // namespace

bool HimsScanApp::begin() {
  Serial.println("HIMS Scan R1 booting");
  display_.begin();
  display_.showBoot("KEYPAD");
  display_.tick();
  keypadInit();
  display_.showBoot("SCANNER");
  display_.tick();
  scanner_.begin(9600);
  scanner_.flushInput();
  powerMode_ = PowerMode::Normal;
  otaRequested_ = false;
  otaActive_ = false;
  lastSyncAttempt_ = 0;
  syncSequence_ = 1;

  clientConfig_.deviceId = DEVICE_ID;
  clientConfig_.token = DEVICE_TOKEN;
  clientConfig_.fallbackHost = FALLBACK_HOST;
  clientConfig_.fallbackPort = FALLBACK_PORT;

  outbox_.begin("hims_scan", DEVICE_ID);
  outbox_.load();

  client_.begin(clientConfig_);
  wifiStarted_ = true;

  if (!WIFI_AUTOSTART) {
    Serial.println("Wi-Fi auto-start disabled; running offline for keypad/scanner testing.");
  } else if (trimCopy(WIFI_SSID).length() == 0) {
    Serial.println("Wi-Fi credentials are empty; staying offline.");
  } else {
    Serial.print("Connecting to local Wi-Fi SSID: ");
    Serial.println(WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }

  sendDebugMessage("boot");
  logState("boot");
  display_.setConnectivity(WiFi.isConnected(), false, outbox_.size());
  display_.showReady();
  display_.tick();
  return true;
}

void HimsScanApp::loop() {
  display_.setConnectivity(WiFi.isConnected(), pcOnline_, outbox_.size());
  display_.tick();
  HimsKeyEvent event;
  while (keypadPoll(event)) {
    handleKey(event);
    if (otaRequested_) {
      break;
    }
  }

  if (powerMode_ == PowerMode::Standby) {
    if (serviceOta()) {
      display_.tick();
      delay(2);
      return;
    }
    display_.tick();
    delay(20);
    return;
  }

  // Drain UART before any Wi-Fi operation that may briefly block.
  pollScanner();

  if (serviceOta()) {
    display_.tick();
    delay(2);
    return;
  }

  reconnectWiFi();
  primeHimsSoftwareConnection();

  maybeSync();
  // Drain anything received while the HTTP client was active.
  pollScanner();
  display_.setConnectivity(WiFi.isConnected(), pcOnline_, outbox_.size());
  display_.tick();
  delay(2);
}

void HimsScanApp::pollScanner() {
  String code;
  if (scanner_.poll(code)) {
    handleScan(code);
  }
}

void HimsScanApp::handleScan(const String& code) {
  // Some GM65 profiles prepend an AIM symbology identifier (for example ]C1
  // for Code 128, ]Q3 for QR, or ]d2 for Data Matrix). We use that marker to
  // route Data Matrix scans to DigiKey and QR scans to the HIMS path.
  const auto raw = trimCopy(code);
  const auto trimmed = trimCopy(stripAimSymbologyPrefix(raw));
  if (trimmed.length() == 0) {
    return;
  }

  Serial.print("GM65 scan: ");
  Serial.println(trimmed.c_str());
  display_.pulseScan();

  if (isDataMatrixScan(raw)) {
    const auto lookupCode = digikeyLookupCodeFromDataMatrix(trimmed);
    const auto forwardedCode = lookupCode.length() > 0 ? lookupCode : trimmed;
    const auto defaultQuantity = digikeyQuantityFromDataMatrix(trimmed);
    resetPending();
    scannedCode_ = forwardedCode;
    quantity_.setDefaultValue(defaultQuantity);
    pendingScanType_ = PendingScanType::DigiKeyDataMatrix;
    setAwaitingQuantity();
    display_.showQuantity(scannedCode_, quantity_.valueOrOne(), true);
    Serial.print("DigiKey default quantity: ");
    Serial.println(quantity_.displayText().c_str());
    Serial.println("Enter quantity, then press A to add. C cancels.");
    sendDebugMessage(String("data matrix pending ") + forwardedCode + " qty " + quantity_.displayText(), "scan");
    return;
  }

  if (isQrScan(raw) || isQuantityCode(trimmed)) {
    scannedCode_ = trimmed;
    quantity_.clear();
    pendingScanType_ = PendingScanType::HimsQuantity;
    setAwaitingQuantity();
    display_.showQuantity(scannedCode_, quantity_.valueOrOne(), false);
    Serial.println("Enter quantity, then press A to add or B to subtract. C cancels.");
    sendDebugMessage(String("qr quantity scan ") + trimmed, "scan");
    return;
  }

  resetPending();
  if (queueEvent(DeviceEventKind::InventoryReceive, trimmed, 1)) {
    display_.showQueued();
    Serial.println("Queued supplier scan for HIMS software.");
    sendDebugMessage(String("scan queued ") + trimmed, "scan");
  }
}

void HimsScanApp::handleKey(const HimsKeyEvent& event) {
  if (event.type == HimsKeyEventType::OtaUpdate) {
    requestOtaUpdate();
    return;
  }

  if (event.type == HimsKeyEventType::PowerToggle) {
    if (powerMode_ == PowerMode::Standby) {
      exitStandby();
    } else {
      enterStandby();
    }
    return;
  }

  if (event.type == HimsKeyEventType::Digit) {
    if (state_ == State::Idle) {
      Serial.println("Digit ignored, scan a code first.");
      return;
    }
    quantity_.appendDigit(event.value);
    display_.updateQuantity(quantity_.valueOrOne());
    Serial.print("Quantity: ");
    Serial.println(quantity_.displayText().c_str());
    return;
  }

  if (event.type == HimsKeyEventType::Cancel) {
    if (state_ == State::Idle && display_.errorVisible()) {
      display_.dismissError();
      return;
    }
    resetPending();
    display_.finishInteraction();
    Serial.println("Cancelled pending scan.");
    return;
  }

  if (event.type == HimsKeyEventType::Add || event.type == HimsKeyEventType::Subtract) {
    if (state_ == State::Idle || scannedCode_.length() == 0) {
      Serial.println("No scanned code to submit.");
      return;
    }
    if (pendingScanType_ == PendingScanType::DigiKeyDataMatrix && event.type == HimsKeyEventType::Subtract) {
      Serial.println("DigiKey scans use A to confirm the quantity.");
      return;
    }
    submitCurrent(event.type == HimsKeyEventType::Add ? 'A' : 'B');
  }
}

void HimsScanApp::requestOtaUpdate() {
  if (otaRequested_) {
    Serial.println("OTA update already requested.");
    return;
  }

  resetPending();
  otaRequested_ = true;
  otaActive_ = false;
  client_.end();
  scanner_.flushInput();
  if (powerMode_ == PowerMode::Standby) {
    Serial.println("Waking standby path for OTA update.");
  }
  Serial.println("OTA update requested. Normal scanning is paused until the update finishes.");
  display_.showOta("CONNECTING", 0);
  sendDebugMessage("ota requested", "warn");
}

bool HimsScanApp::serviceOta() {
  if (!otaRequested_) {
    return false;
  }

  if (trimCopy(WIFI_SSID).length() == 0) {
    Serial.println("OTA request ignored because Wi-Fi credentials are empty.");
    otaRequested_ = false;
    otaActive_ = false;
    return false;
  }

  if (!ensureWiFiForOta()) {
    return true;
  }

  if (!otaActive_) {
    if (!startOtaService()) {
      Serial.println("OTA service failed to start; resuming normal operation.");
      otaRequested_ = false;
      otaActive_ = false;
      return false;
    }
  }

  ArduinoOTA.handle();
  return true;
}

bool HimsScanApp::ensureWiFiForOta() {
  if (WiFi.isConnected()) {
    return true;
  }

  if (trimCopy(WIFI_SSID).length() == 0) {
    Serial.println("OTA request ignored because Wi-Fi credentials are empty.");
    otaRequested_ = false;
    otaActive_ = false;
    return false;
  }

  if (WiFi.getMode() != WIFI_STA) {
    WiFi.mode(WIFI_STA);
    delay(50);
  }

  if (millis() - lastReconnectAttempt_ < 2000UL) {
    return false;
  }
  lastReconnectAttempt_ = millis();

  Serial.print("Connecting for OTA to local Wi-Fi SSID: ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  return false;
}

bool HimsScanApp::startOtaService() {
  WiFi.setSleep(false);
  ArduinoOTA.setHostname(clientConfig_.deviceId.c_str());
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([this]() {
    otaActive_ = true;
    Serial.println("OTA start: flashing new firmware.");
    display_.showOta("UPDATING", 0);
  });
  ArduinoOTA.onEnd([this]() {
    Serial.println();
    Serial.println("OTA complete; restarting device.");
    otaRequested_ = false;
    otaActive_ = false;
    display_.showOta("RESTARTING", 100);
    display_.tick();
    delay(250);
    ESP.restart();
  });
  ArduinoOTA.onProgress([this](unsigned int progress, unsigned int total) {
    if (total == 0U) {
      return;
    }
    const unsigned int percent = (progress * 100U) / total;
    Serial.printf("OTA progress: %u%%\r", percent);
    display_.showOta("UPDATING", static_cast<int>(percent));
  });
  ArduinoOTA.onError([this](ota_error_t error) {
    Serial.print("OTA error: ");
    switch (error) {
      case OTA_AUTH_ERROR:
        Serial.println("authentication failed");
        break;
      case OTA_BEGIN_ERROR:
        Serial.println("begin failed");
        break;
      case OTA_CONNECT_ERROR:
        Serial.println("connection failed");
        break;
      case OTA_RECEIVE_ERROR:
        Serial.println("receive failed");
        break;
      case OTA_END_ERROR:
        Serial.println("end failed");
        break;
      default:
        Serial.println("unknown");
        break;
    }
    SyncResult result;
    result.status = "failed";
    result.code = "OTA ERROR";
    result.message = "Update failed";
    result.resultId = "local-ota";
    result.eventId = "local-ota";
    display_.showResult(result);
  });

  ArduinoOTA.begin();
  otaActive_ = true;
  Serial.print("OTA update ready for ");
  Serial.print(clientConfig_.deviceId.c_str());
  Serial.print(" at ");
  Serial.println(WiFi.localIP());
  sendDebugMessage(String("ota ready at ") + WiFi.localIP().toString(), "warn");
  return true;
}

void HimsScanApp::sendDebugMessage(const String& message, const String& level) {
  if (!WiFi.isConnected() || trimCopy(message).length() == 0) {
    return;
  }
  client_.postDebug(message, level);
}

String HimsScanApp::buildWifiDebugText() const {
  String out;
  if (powerMode_ == PowerMode::Standby) {
    out = "standby";
  } else if (!WIFI_AUTOSTART) {
    out = "wifi autostart disabled";
  } else if (trimCopy(WIFI_SSID).length() == 0) {
    out = "wifi credentials empty";
  } else if (WiFi.status() == WL_CONNECTED) {
    out = "wifi connected ssid=";
    out += WiFi.SSID();
    out += " ip=";
    out += WiFi.localIP().toString();
  } else {
    out = "wifi disconnected target=";
    out += WIFI_SSID;
  }

  out += " endpoint=";
  out += client_.endpointSummary();
  if (otaRequested_) {
    out += " ota=pending";
  }
  return out;
}

void HimsScanApp::enterStandby() {
  if (powerMode_ == PowerMode::Standby) {
    return;
  }

  Serial.println("Entering standby mode.");
  resetPending();
  otaRequested_ = false;
  otaActive_ = false;
  lastReconnectAttempt_ = 0;
  lastSyncAttempt_ = 0;
  wifiConnectedReported_ = false;
  himsSoftwarePrimed_ = false;

  client_.end();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  scanner_.suspend();
  powerMode_ = PowerMode::Standby;
  pcOnline_ = false;
  display_.setStandby(true);
  Serial.println("Standby mode active. Hold D again to wake.");
  sendDebugMessage("standby entered", "warn");
}

void HimsScanApp::exitStandby() {
  if (powerMode_ != PowerMode::Standby) {
    return;
  }

  Serial.println("Waking from standby mode.");
  scanner_.resume();
  client_.begin(clientConfig_);
  wifiStarted_ = true;
  wifiConnectedReported_ = false;
  himsSoftwarePrimed_ = false;
  lastReconnectAttempt_ = 0;
  lastSyncAttempt_ = 0;
  powerMode_ = PowerMode::Normal;
  display_.setStandby(false);

  if (WIFI_AUTOSTART && trimCopy(WIFI_SSID).length() > 0) {
    Serial.print("Reconnecting after standby to local Wi-Fi SSID: ");
    Serial.println(WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  } else {
    Serial.println("Standby exit complete; Wi-Fi remains offline.");
  }
  sendDebugMessage("standby exited", "warn");
}

void HimsScanApp::submitCurrent(char action) {
  if (pendingScanType_ == PendingScanType::DigiKeyDataMatrix) {
    if (action != 'A') {
      Serial.println("DigiKey scans require A to confirm.");
      return;
    }

    const auto quantity = quantity_.consume(true);
    const auto queued = queueEvent(DeviceEventKind::InventoryReceive, scannedCode_, quantity);
    Serial.print("Queued DigiKey scan quantity ");
    Serial.println(String(quantity).c_str());
    resetPending();
    if (!queued) {
      display_.finishInteraction();
      return;
    }
    const bool resultShown = display_.finishInteraction();
    if (queued && !resultShown) display_.showQueued();
    return;
  }

  const auto delta = quantity_.consume(action == 'A');
  const auto queued = queueEvent(DeviceEventKind::InventoryAdjust, scannedCode_, delta);
  resetPending();
  if (!queued) {
    display_.finishInteraction();
    return;
  }
  const bool resultShown = display_.finishInteraction();
  if (queued && !resultShown) display_.showQueued();
}

bool HimsScanApp::queueEvent(DeviceEventKind kind, const String& code, int value) {
  DeviceEventRecord event;
  if (!outbox_.enqueue(kind, code, value, &event)) {
    SyncResult result;
    result.resultId = "local-queue";
    result.eventId = "local-queue";
    result.status = "failed";
    result.code = outbox_.full() ? "QUEUE FULL" : "QUEUE ERROR";
    result.message = outbox_.full() ? "Sync PC before scanning" : "Could not save event";
    display_.showResult(result);
    Serial.println(result.message.c_str());
    return false;
  }
  Serial.print("Queued protocol v1 event: ");
  Serial.println(event.eventId.c_str());
  lastSyncAttempt_ = 0;
  return true;
}

void HimsScanApp::reconnectWiFi() {
  if (!wifiStarted_) {
    return;
  }

  if (powerMode_ == PowerMode::Standby) {
    return;
  }

  if (!WIFI_AUTOSTART && !otaRequested_ && !otaActive_) {
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiConnectedReported_) {
      wifiConnectedReported_ = true;
      himsSoftwarePrimed_ = false;
      lastSyncAttempt_ = 0;
      Serial.print("Local Wi-Fi connected: ");
      Serial.print(WiFi.SSID());
      Serial.print(" @ ");
      Serial.println(WiFi.localIP());
      sendDebugMessage(buildWifiDebugText(), "status");
    }
    return;
  }

  wifiConnectedReported_ = false;
  himsSoftwarePrimed_ = false;
  pcOnline_ = false;
  if (millis() - lastReconnectAttempt_ < WIFI_RECONNECT_INTERVAL_MS) {
    return;
  }
  lastReconnectAttempt_ = millis();

  if (trimCopy(WIFI_SSID).length() == 0) {
    Serial.println("Wi-Fi credentials are empty; staying offline.");
    return;
  }

  Serial.print("Reconnecting to local Wi-Fi SSID: ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void HimsScanApp::primeHimsSoftwareConnection() {
  if (!wifiConnectedReported_ || himsSoftwarePrimed_) {
    return;
  }

  himsSoftwarePrimed_ = true;
  Serial.println("Local Wi-Fi ready; discovering HIMS software endpoint...");
  if (client_.resolveEndpoint(true)) {
    Serial.print("HIMS software endpoint: ");
    Serial.println(client_.endpointSummary().c_str());
  }
}

String HimsScanApp::syncMode() const {
  if (otaRequested_ || otaActive_) return "ota";
  if (powerMode_ == PowerMode::Standby) return "standby";
  if (state_ == State::AwaitQuantity) return "await_quantity";
  return "ready";
}

void HimsScanApp::maybeSync() {
  if (!WiFi.isConnected()) {
    pcOnline_ = false;
    return;
  }
  const unsigned long interval = (!outbox_.empty() || pendingResultAck_.length() > 0) ? 1000UL : 5000UL;
  if (lastSyncAttempt_ != 0 && millis() - lastSyncAttempt_ < interval) return;
  lastSyncAttempt_ = millis();
  if (!client_.resolveEndpoint()) {
    pcOnline_ = false;
    return;
  }

  DeviceEventRecord events[kSyncBatchSize];
  std::size_t eventCount = 0;
  while (eventCount < kSyncBatchSize && outbox_.peek(eventCount, events[eventCount])) ++eventCount;
  String acknowledgements[1];
  const std::size_t ackCount = pendingResultAck_.length() > 0 ? 1U : 0U;
  if (ackCount > 0) acknowledgements[0] = pendingResultAck_;
  const auto requestId = clientConfig_.deviceId + String("-sync-") + String(syncSequence_++);
  const auto payload = buildSyncRequestJson(requestId, clientConfig_.deviceId, kFirmwareVersion, syncMode(),
                                            WiFi.RSSI(), outbox_.size(), events, eventCount,
                                            acknowledgements, ackCount);
  int status = 0;
  String body;
  const auto sent = client_.postSync(payload, status, body);
  if (!sent || status != 200) {
    pcOnline_ = false;
    Serial.print("POST /api/v1/device/sync failed, status ");
    Serial.println(status);
    if ((status == 401 || status == 426) && !outbox_.empty()) {
      SyncResult result;
      result.resultId = "local-protocol";
      result.eventId = "local-protocol";
      result.status = "failed";
      result.code = status == 426 ? "UPDATE PC" : "PAIRING ERROR";
      result.message = status == 426 ? "Protocol v1 required" : "Check device token";
      display_.showResult(result);
    }
    return;
  }

  const auto response = parseSyncResponseJson(body);
  if (!response.valid || response.requestId != requestId) {
    pcOnline_ = false;
    Serial.println("Invalid protocol v1 sync response.");
    return;
  }
  pcOnline_ = true;
  if (ackCount > 0 && pendingResultAck_ == acknowledgements[0]) pendingResultAck_.clear();
  handleSyncResponse(response);
}

void HimsScanApp::handleSyncResponse(const SyncResponse& response) {
  while (!outbox_.empty()) {
    DeviceEventRecord front;
    if (!outbox_.peek(0, front)) break;
    bool accepted = false;
    for (std::size_t index = 0; index < response.acceptedCount; ++index) {
      if (response.acceptedEventIds[index] == front.eventId) {
        accepted = true;
        break;
      }
    }
    if (!accepted) break;
    outbox_.pop();
  }
  display_.setConnectivity(WiFi.isConnected(), true, outbox_.size());
  if (response.hasResult && display_.showResult(response.result)) {
    pendingResultAck_ = response.result.resultId;
    lastSyncAttempt_ = 0;
  }
}

void HimsScanApp::logState(const char* reason) const {
  Serial.print("State update [");
  Serial.print(reason);
  Serial.print("]: ");
  Serial.println(state_ == State::Idle ? "idle" : "awaiting quantity");
}

void HimsScanApp::setAwaitingQuantity() {
  state_ = State::AwaitQuantity;
}

void HimsScanApp::resetPending() {
  scannedCode_.clear();
  quantity_.clear();
  state_ = State::Idle;
  pendingScanType_ = PendingScanType::None;
}

}  // namespace hims_scan
