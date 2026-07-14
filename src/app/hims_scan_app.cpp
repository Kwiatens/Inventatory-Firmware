// HIMS Scan R1 application controller.

#include "app/hims_scan_app.h"

#include <ArduinoOTA.h>

#include "HimsScanUiPolicy.h"

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
  display_.showSplash();
  display_.tick();
  keypadInit();
  scanner_.begin(9600);
  scanner_.flushInput();
  display_.showBoot({});
  display_.tick();
  powerMode_ = PowerMode::Normal;
  otaRequested_ = false;
  otaActive_ = false;
  lastSyncAttempt_ = 0;
  syncSequence_ = 1;
  lookupSequence_ = 1;

  clientConfig_.deviceId = DEVICE_ID;
  clientConfig_.fallbackHost = FALLBACK_HOST;
  clientConfig_.fallbackPort = FALLBACK_PORT;

  outbox_.begin("hims_scan", DEVICE_ID);
  outbox_.load();
  if (!deviceSettings_.begin()) {
    Serial.println("Device settings storage unavailable; using default contrast.");
  } else {
    lcdContrast_ = deviceSettings_.contrast();
    display_.setContrast(lcdContrast_);
  }
  if (!setupStore_.begin() || !setupStore_.load(provisionedSettings_)) {
    setupMode_ = true;
    if (!provisioning_.begin(clientConfig_.deviceId)) {
      Serial.println("BLE provisioning could not start");
      return false;
    }
    display_.showSetup(provisioning_.pairingCode());
    display_.tick();
    return true;
  }
  return beginNormalRuntime(provisionedSettings_);
}

void HimsScanApp::loop() {
  if (setupMode_) {
    HimsKeyEvent event;
    while (keypadPoll(event)) handleKey(event);
    serviceProvisioning();
    display_.tick();
    delay(10);
    return;
  }
  updateConnectivityDisplay();
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

  processNetworkResult();
  maybeSync();
  // Drain anything received while the HTTP client was active.
  pollScanner();
  updateConnectivityDisplay();
  display_.tick();
  delay(2);
}

bool HimsScanApp::beginNormalRuntime(const ProvisionedSettings& settings) {
  provisionedSettings_ = settings;
  clientConfig_.token = settings.deviceToken;
  if (!startNetworkWorker()) return false;
  sendDebugMessage("boot");
  logState("boot");
  updateConnectivityDisplay();
  display_.showReady();
  display_.tick();
  return true;
}

void HimsScanApp::serviceProvisioning() {
  ProvisionedSettings request;
  if (provisioning_.takeRequest(request)) {
    provisionedSettings_ = request;
    setupWifiConnecting_ = true;
    setupWifiStartedAt_ = millis();
    WiFi.mode(WIFI_STA);
    WiFi.begin(request.wifiSsid.c_str(), request.wifiPassword.c_str());
    display_.showSetup(provisioning_.pairingCode(), "CONNECTING WIFI");
  }
  if (!setupWifiConnecting_) return;
  if (WiFi.status() == WL_CONNECTED) {
    setupWifiConnecting_ = false;
    if (!setupStore_.save(provisionedSettings_)) {
      provisioning_.setResult(false, "SAVE FAILED");
      display_.showSetup(provisioning_.pairingCode(), "SAVE FAILED");
      WiFi.disconnect(false);
      return;
    }
    provisioning_.setResult(true, "WIFI OK");
    provisioning_.end();
    setupMode_ = false;
    if (!beginNormalRuntime(provisionedSettings_)) {
      setupMode_ = true;
      provisioning_.begin(clientConfig_.deviceId);
      display_.showSetup(provisioning_.pairingCode(), "NETWORK ERROR");
    }
    return;
  }
  if (millis() - setupWifiStartedAt_ > 20000UL) {
    setupWifiConnecting_ = false;
    WiFi.disconnect(false);
    provisioning_.setResult(false, "WIFI FAILED");
    display_.showSetup(provisioning_.pairingCode(), "WIFI FAILED");
  }
}

void HimsScanApp::pollScanner() {
  String code;
  if (scanner_.poll(code)) {
    handleScan(code);
  }
}

void HimsScanApp::handleScan(const String& code) {
  if (!scanMayInterruptUi(setupMode_, powerMode_ == PowerMode::Standby,
                          otaRequested_ || otaActive_, state_ == State::ConfirmReprovision,
                          state_ == State::PrintingLabel)) {
    Serial.println("Scan ignored while the current operation blocks scanning.");
    return;
  }
  if (state_ != State::Idle) resetPending();
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
  if (isDataMatrixScan(raw)) {
    const auto lookupCode = digikeyLookupCodeFromDataMatrix(trimmed);
    if (!hasIso15434Macro06Envelope(trimmed) || !isStandardSupplierPartCode(lookupCode)) {
      Serial.println("Rejected non-standard Data Matrix scan.");
      return;
    }
    const auto forwardedCode = lookupCode;
    const auto defaultQuantity = digikeyQuantityFromDataMatrix(trimmed);
    resetPending();
    scannedCode_ = forwardedCode;
    quantity_.setDefaultValue(defaultQuantity);
    pendingScanType_ = PendingScanType::DigiKeyDataMatrix;
    setAwaitingQuantity();
    display_.showQuantity(scannedCode_, "DigiKey", quantity_.displayText(), quantity_.cursor(), true);
    Serial.print("DigiKey default quantity: ");
    Serial.println(quantity_.displayText().c_str());
    Serial.println("Enter quantity, then press A to add. C cancels.");
    sendDebugMessage(String("data matrix pending ") + forwardedCode + " qty " + quantity_.displayText(), "scan");
    return;
  }

  if (isQrScan(raw) || isQuantityCode(trimmed)) {
    if (!isStandardMachineCode(trimmed)) {
      Serial.println("Rejected non-standard inventory ID scan.");
      return;
    }
    resetPending();
    scannedCode_ = trimmed;
    quantity_.setDefaultValue(1);
    pendingScanType_ = PendingScanType::HimsQuantity;
    activeLookupCode_ = trimmed;
    activeLookupId_ = clientConfig_.deviceId + String("-lookup-") + String(lookupSequence_++);
    lookupMissCount_ = 0;
    setAwaitingQuantity();
    display_.showQuantity(WiFi.isConnected() ? String("Looking up item") : String("Inventory item"),
                          String("Code ") + trimmed, quantity_.displayText(), quantity_.cursor(), false);
    lastSyncAttempt_ = 0;
    Serial.println("Enter quantity, then press A to add or B to subtract. C cancels.");
    sendDebugMessage(String("qr quantity scan ") + trimmed, "scan");
    return;
  }

  if (!isStandardSupplierPartCode(trimmed)) {
    Serial.println("Rejected unrecognized scanner data.");
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
  if (event.type == HimsKeyEventType::ProvisioningReset) {
    if (setupStore_.clear()) {
      Serial.println("Provisioning reset requested; restarting into BLE setup.");
      ESP.restart();
    }
    Serial.println("Provisioning reset could not clear NVS.");
    return;
  }
  if (event.type == HimsKeyEventType::OtaUpdate) {
    requestOtaUpdate();
    return;
  }

  if (event.type == HimsKeyEventType::PowerToggle) {
    if (state_ != State::Idle) return;
    if (powerMode_ == PowerMode::Standby) {
      exitStandby();
    } else {
      enterStandby();
    }
    return;
  }

  if (event.type == HimsKeyEventType::Digit) {
    if (state_ == State::Menu) {
      if (event.value == '1') openQuickLabels();
      else if (event.value == '2') openQuickSettings();
      return;
    }
    if (state_ == State::QuickLabels) {
      const uint8_t selected = static_cast<uint8_t>(event.value - '0');
      if (selected >= 1 && selected <= 4) requestQuickLabelPrint(quickLabelPage_ * 4U + selected);
      return;
    }
    if (state_ == State::QuickSettings) {
      if (event.value == '1') {
        state_ = State::AdjustContrast;
        display_.showContrast(lcdContrast_);
      } else if (event.value == '2') {
        state_ = State::Idle;
        enterStandby();
      } else if (event.value == '3') {
        state_ = State::ConfirmReprovision;
        display_.showResetConfirm();
      } else if (event.value == '4') {
        state_ = State::Idle;
        requestOtaUpdate();
      }
      return;
    }
    if (state_ == State::Idle) {
      Serial.println("Digit ignored, scan a code first.");
      return;
    }
    quantity_.appendDigit(event.value);
    display_.updateQuantity(quantity_.displayText(), quantity_.cursor());
    Serial.print("Quantity: ");
    Serial.println(quantity_.displayText().c_str());
    return;
  }

  if (event.type == HimsKeyEventType::CycleCursor) {
    if (state_ == State::QuickLabels && quickLabelPresetCount_ > 4U) {
      const uint8_t pages = static_cast<uint8_t>((quickLabelPresetCount_ + 3U) / 4U);
      quickLabelPage_ = static_cast<uint8_t>((quickLabelPage_ + 1U) % pages);
      display_.showQuickLabels(quickLabelPresets_, quickLabelPresetCount_, quickLabelPage_,
                               quickLabelSnapshotReceived_, pcSupportsQuickLabels_);
      return;
    }
    if (state_ == State::AwaitQuantity) {
      quantity_.cycleCursor();
      display_.updateQuantity(quantity_.displayText(), quantity_.cursor());
    }
    return;
  }

  if (event.type == HimsKeyEventType::Cancel) {
    if (state_ == State::Idle) {
      if (display_.errorVisible()) display_.dismissError();
      else {
        state_ = State::Menu;
        display_.showMenu();
      }
      return;
    }
    if (state_ == State::Menu || state_ == State::QuickLabels || state_ == State::QuickSettings ||
        state_ == State::PrintingLabel) {
      pendingQuickLabelPrintId_.clear();
      pendingQuickLabelPresetIndex_ = 0;
      state_ = State::Idle;
      display_.showReady();
      return;
    }
    if (state_ == State::AdjustContrast) {
      persistContrast();
      openQuickSettings();
      return;
    }
    if (state_ == State::ConfirmReprovision) {
      openQuickSettings();
      return;
    }
    resetPending();
    display_.finishInteraction();
    Serial.println("Cancelled pending scan.");
    return;
  }

  if (event.type == HimsKeyEventType::Add || event.type == HimsKeyEventType::Subtract) {
    if (state_ == State::AdjustContrast) {
      if (event.type == HimsKeyEventType::Add && lcdContrast_ > 0) --lcdContrast_;
      if (event.type == HimsKeyEventType::Subtract && lcdContrast_ < 63) ++lcdContrast_;
      display_.setContrast(lcdContrast_);
      persistContrast();
      display_.showContrast(lcdContrast_);
      return;
    }
    if (state_ == State::ConfirmReprovision && event.type == HimsKeyEventType::Add) {
      if (setupStore_.clear()) {
        Serial.println("Re-pair requested from quick settings; restarting BLE setup.");
        ESP.restart();
      }
      display_.showQuickLabelPrintResult(false, "RESET FAILED");
      state_ = State::Idle;
      return;
    }
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
  networkPowerOffRequested_ = false;
  networkPaused_ = true;
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

  if (provisionedSettings_.wifiSsid.length() == 0) {
    Serial.println("OTA request ignored because setup is incomplete.");
    otaRequested_ = false;
    otaActive_ = false;
    networkPaused_ = false;
    return false;
  }

  if (!networkPauseAcknowledged_) return true;

  if (!ensureWiFiForOta()) {
    return true;
  }

  if (!otaActive_) {
    if (!startOtaService()) {
      Serial.println("OTA service failed to start; resuming normal operation.");
      otaRequested_ = false;
      otaActive_ = false;
      networkPaused_ = false;
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

  if (provisionedSettings_.wifiSsid.length() == 0) {
    Serial.println("OTA request ignored because setup is incomplete.");
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

  Serial.println("Connecting for OTA to saved local Wi-Fi.");
  WiFi.begin(provisionedSettings_.wifiSsid.c_str(), provisionedSettings_.wifiPassword.c_str());
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
  if (networkPaused_ || !WiFi.isConnected() || trimCopy(message).length() == 0) {
    return;
  }
  if (networkMutex_ == nullptr || xSemaphoreTake(networkMutex_, pdMS_TO_TICKS(5)) != pdTRUE) return;
  if (!debugJobPending_) {
    debugJobMessage_ = message;
    debugJobLevel_ = level;
    debugJobPending_ = true;
  }
  xSemaphoreGive(networkMutex_);
}

bool HimsScanApp::startNetworkWorker() {
  if (networkTask_ != nullptr) return true;
  networkMutex_ = xSemaphoreCreateMutex();
  if (networkMutex_ == nullptr) {
    Serial.println("Network worker: mutex allocation failed");
    return false;
  }
  const auto created = xTaskCreatePinnedToCore(networkTaskEntry, "inventatory-net", 8192, this, 1,
                                                &networkTask_, 0);
  if (created != pdPASS) {
    Serial.println("Network worker: task allocation failed");
    vSemaphoreDelete(networkMutex_);
    networkMutex_ = nullptr;
    networkTask_ = nullptr;
    return false;
  }
  return true;
}

void HimsScanApp::networkTaskEntry(void* context) {
  static_cast<HimsScanApp*>(context)->networkTaskLoop();
}

void HimsScanApp::networkTaskLoop() {
  client_.begin(clientConfig_);
  wifiStarted_ = true;
  networkStarted_ = true;
  for (;;) {
    if (networkPaused_) {
      if (!networkPauseAcknowledged_) {
        client_.end();
        if (networkPowerOffRequested_) {
          WiFi.disconnect(true);
          WiFi.mode(WIFI_OFF);
          wifiStarted_ = false;
          wifiConnectedReported_ = false;
        }
        networkPauseAcknowledged_ = true;
      }
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    if (networkPauseAcknowledged_) {
      networkPauseAcknowledged_ = false;
      client_.begin(clientConfig_);
      wifiStarted_ = true;
      lastReconnectAttempt_ = 0;
      wifiConnectedReported_ = false;
    }

    reconnectWiFiWorker();

    String requestId;
    String payload;
    String debugMessage;
    String debugLevel;
    bool hasSync = false;
    bool hasDebug = false;
    if (xSemaphoreTake(networkMutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
      if (syncJobPending_) {
        requestId = syncJobRequestId_;
        payload = syncJobPayload_;
        syncJobPending_ = false;
        syncJobRequestId_.clear();
        syncJobPayload_.clear();
        hasSync = true;
      } else if (debugJobPending_) {
        debugMessage = debugJobMessage_;
        debugLevel = debugJobLevel_;
        debugJobPending_ = false;
        debugJobMessage_.clear();
        debugJobLevel_.clear();
        hasDebug = true;
      }
      xSemaphoreGive(networkMutex_);
    }

    if (hasSync) {
      int status = 0;
      String body;
      networkBusy_ = true;
      const bool sent = WiFi.isConnected() && client_.postSync(payload, status, body);
      networkBusy_ = false;
      if (xSemaphoreTake(networkMutex_, portMAX_DELAY) == pdTRUE) {
        syncResultRequestId_ = requestId;
        syncResultSent_ = sent;
        syncResultStatus_ = status;
        syncResultBody_ = body;
        syncResultPending_ = true;
        xSemaphoreGive(networkMutex_);
      }
    } else if (hasDebug && WiFi.isConnected()) {
      networkBusy_ = true;
      client_.postDebug(debugMessage, debugLevel);
      networkBusy_ = false;
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

bool HimsScanApp::queueSyncJob(const String& requestId, const String& payload) {
  if (!networkStarted_ || networkMutex_ == nullptr || xSemaphoreTake(networkMutex_, pdMS_TO_TICKS(5)) != pdTRUE) {
    return false;
  }
  const bool available = !syncJobPending_ && !syncResultPending_;
  if (available) {
    syncJobRequestId_ = requestId;
    syncJobPayload_ = payload;
    syncJobPending_ = true;
  }
  xSemaphoreGive(networkMutex_);
  return available;
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

  networkPowerOffRequested_ = true;
  networkPaused_ = true;
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
  networkPowerOffRequested_ = false;
  networkPaused_ = false;
  wifiConnectedReported_ = false;
  himsSoftwarePrimed_ = false;
  lastReconnectAttempt_ = 0;
  lastSyncAttempt_ = 0;
  powerMode_ = PowerMode::Normal;
  display_.setStandby(false);

  if (provisionedSettings_.wifiSsid.length() == 0) {
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

void HimsScanApp::reconnectWiFiWorker() {
  if (!wifiStarted_) {
    return;
  }

  if (powerMode_ == PowerMode::Standby) {
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiConnectedReported_) {
      wifiConnectedReported_ = true;
      himsSoftwarePrimed_ = false;
      lastSyncAttempt_ = 0;
      // Do not disclose provisioning metadata through the serial console.
      Serial.println("Local Wi-Fi connected");
    }
    return;
  }

  wifiConnectedReported_ = false;
  himsSoftwarePrimed_ = false;
  pcOnline_ = false;
  if (lastReconnectAttempt_ != 0 && millis() - lastReconnectAttempt_ < WIFI_RECONNECT_INTERVAL_MS) {
    return;
  }
  lastReconnectAttempt_ = millis();

  if (provisionedSettings_.wifiSsid.length() == 0) {
    Serial.println("Wi-Fi setup is incomplete; staying offline.");
    return;
  }

  Serial.println("Reconnecting to saved local Wi-Fi.");
  WiFi.begin(provisionedSettings_.wifiSsid.c_str(), provisionedSettings_.wifiPassword.c_str());
}

String HimsScanApp::syncMode() const {
  if (otaRequested_ || otaActive_) return "ota";
  if (powerMode_ == PowerMode::Standby) return "standby";
  if (state_ == State::AwaitQuantity) return "await_quantity";
  if (state_ == State::PrintingLabel) return "label_print";
  return "ready";
}

void HimsScanApp::maybeSync() {
  if (!WiFi.isConnected()) {
    pcOnline_ = false;
    return;
  }
  const unsigned long interval = (!outbox_.empty() || pendingResultAck_.length() > 0 ||
                                  pendingQuickLabelPrintId_.length() > 0 ||
                                  (state_ == State::QuickLabels && !quickLabelSnapshotReceived_))
                                     ? 1000UL
                                     : 5000UL;
  if (syncInFlight_ || (lastSyncAttempt_ != 0 && millis() - lastSyncAttempt_ < interval)) return;

  DeviceEventRecord events[kSyncBatchSize];
  std::size_t eventCount = 0;
  while (eventCount < kSyncBatchSize && outbox_.peek(eventCount, events[eventCount])) ++eventCount;
  String acknowledgements[1];
  const std::size_t ackCount = pendingResultAck_.length() > 0 ? 1U : 0U;
  if (ackCount > 0) acknowledgements[0] = pendingResultAck_;
  QuickLabelPrintRequest quickLabelPrint;
  const QuickLabelPrintRequest* quickLabelPrintPtr = nullptr;
  if (pendingQuickLabelPrintId_.length() > 0) {
    quickLabelPrint.requestId = pendingQuickLabelPrintId_;
    quickLabelPrint.presetIndex = pendingQuickLabelPresetIndex_;
    quickLabelPrint.revision = pendingQuickLabelRevision_;
    quickLabelPrintPtr = &quickLabelPrint;
  }
  const auto requestId = clientConfig_.deviceId + String("-sync-") + String(syncSequence_++);
  const auto payload = buildSyncRequestJson(requestId, clientConfig_.deviceId, kFirmwareVersion, syncMode(),
                                            WiFi.RSSI(), outbox_.size(), events, eventCount,
                                            acknowledgements, ackCount, activeLookupId_, activeLookupCode_, quickLabelPrintPtr);
  if (queueSyncJob(requestId, payload)) {
    syncInFlight_ = true;
    lastSyncAttempt_ = millis();
  }
}

void HimsScanApp::processNetworkResult() {
  if (networkMutex_ == nullptr || xSemaphoreTake(networkMutex_, 0) != pdTRUE) return;
  if (!syncResultPending_) {
    xSemaphoreGive(networkMutex_);
    return;
  }
  const auto requestId = syncResultRequestId_;
  const auto sent = syncResultSent_;
  const auto status = syncResultStatus_;
  const auto body = syncResultBody_;
  syncResultPending_ = false;
  syncResultBody_.clear();
  xSemaphoreGive(networkMutex_);
  syncInFlight_ = false;

  if (!sent || status != 200) {
    pcOnline_ = false;
    Serial.print("POST /api/v1/device/sync failed, status ");
    Serial.println(status);
    if (pendingQuickLabelPrintId_.length() > 0) {
      pendingQuickLabelPrintId_.clear();
      pendingQuickLabelPresetIndex_ = 0;
      state_ = State::Idle;
      display_.showQuickLabelPrintResult(false, "PC OR PRINTER OFFLINE");
    }
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
  if (pendingResultAck_.length() > 0) pendingResultAck_.clear();
  handleSyncResponse(response);
}

void HimsScanApp::handleSyncResponse(const SyncResponse& response) {
  if (response.hasQuickLabels) {
    quickLabelSnapshotReceived_ = true;
    pcSupportsQuickLabels_ = true;
    quickLabelPresetCount_ = 0;
    quickLabelRevision_ = response.quickLabelRevision;
    for (std::size_t index = 0; index < response.quickLabelPresetCount && index < kQuickLabelPresetLimit; ++index) {
      if (response.quickLabelPresets[index].length() > 0 &&
          response.quickLabelPresets[index].length() <= kQuickLabelPresetTextLimit) {
        quickLabelPresets_[quickLabelPresetCount_++] = response.quickLabelPresets[index];
      }
    }
    if (quickLabelPage_ * 4U >= quickLabelPresetCount_) quickLabelPage_ = 0;
    if (state_ == State::QuickLabels) {
      display_.showQuickLabels(quickLabelPresets_, quickLabelPresetCount_, quickLabelPage_,
                               quickLabelSnapshotReceived_, pcSupportsQuickLabels_);
    }
  } else if (state_ == State::QuickLabels) {
    // A normal protocol-v1 response without the optional extension means the paired desktop app is older.
    quickLabelSnapshotReceived_ = true;
    pcSupportsQuickLabels_ = false;
    display_.showQuickLabels(quickLabelPresets_, quickLabelPresetCount_, quickLabelPage_,
                             quickLabelSnapshotReceived_, pcSupportsQuickLabels_);
  }
  if (response.hasQuickLabelPrintResult && response.quickLabelPrintRequestId == pendingQuickLabelPrintId_) {
    const bool success = response.quickLabelPrintStatus == "completed";
    const auto message = response.quickLabelPrintMessage.length() > 0 ? response.quickLabelPrintMessage
                                                                       : response.quickLabelPrintCode;
    pendingQuickLabelPrintId_.clear();
    pendingQuickLabelPresetIndex_ = 0;
    state_ = State::Idle;
    display_.showQuickLabelPrintResult(success, message.length() > 0 ? message : "PRINT RESULT UNKNOWN");
  }
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
  if (isCurrentLookupResult(response, activeLookupId_)) {
    lookupMissCount_ = 0;
    if (response.lookupStatus == "found" && response.lookupItemName.length() > 0) {
      display_.updateQuantityItem(response.lookupItemName);
      activeLookupId_.clear();
      activeLookupCode_.clear();
    } else if (response.lookupStatus == "not_found") {
      display_.updateQuantityItem("Unknown item");
      activeLookupId_.clear();
      activeLookupCode_.clear();
    } else if (response.lookupStatus == "unavailable") {
      display_.updateQuantityItem("Name unavailable");
      activeLookupId_.clear();
      activeLookupCode_.clear();
    }
  } else if (activeLookupId_.length() > 0 && ++lookupMissCount_ >= 3U) {
    display_.updateQuantityItem("Name unavailable");
    activeLookupId_.clear();
    activeLookupCode_.clear();
    lookupMissCount_ = 0;
  }
  updateConnectivityDisplay();
  if (response.hasResult && display_.showResult(response.result)) {
    pendingResultAck_ = response.result.resultId;
    lastSyncAttempt_ = 0;
  }
}

void HimsScanApp::logState(const char* reason) const {
  Serial.print("State update [");
  Serial.print(reason);
  Serial.print("]: ");
  Serial.println(state_ == State::Idle ? "idle" : state_ == State::AwaitQuantity ? "awaiting quantity" : "menu");
}

void HimsScanApp::setAwaitingQuantity() {
  state_ = State::AwaitQuantity;
}

void HimsScanApp::resetPending() {
  scannedCode_.clear();
  quantity_.clear();
  activeLookupId_.clear();
  activeLookupCode_.clear();
  lookupMissCount_ = 0;
  state_ = State::Idle;
  pendingScanType_ = PendingScanType::None;
  pendingQuickLabelPrintId_.clear();
  pendingQuickLabelPresetIndex_ = 0;
  pendingQuickLabelRevision_ = 0;
}

void HimsScanApp::openQuickLabels() {
  state_ = State::QuickLabels;
  if (quickLabelPage_ * 4U >= quickLabelPresetCount_) quickLabelPage_ = 0;
  quickLabelSnapshotReceived_ = false;
  pcSupportsQuickLabels_ = false;
  display_.showQuickLabels(quickLabelPresets_, quickLabelPresetCount_, quickLabelPage_,
                           quickLabelSnapshotReceived_, pcSupportsQuickLabels_);
  refreshQuickLabels();
}

void HimsScanApp::openQuickSettings() {
  state_ = State::QuickSettings;
  display_.showQuickSettings(lcdContrast_);
}

void HimsScanApp::refreshQuickLabels() {
  lastSyncAttempt_ = 0;
}

void HimsScanApp::requestQuickLabelPrint(uint8_t presetIndex) {
  if (!quickLabelSnapshotReceived_ || !pcSupportsQuickLabels_ || presetIndex < 1 ||
      presetIndex > quickLabelPresetCount_ || quickLabelRevision_ == 0) {
    display_.showQuickLabelPrintResult(false, "REFRESH PRESETS FIRST");
    state_ = State::Idle;
    return;
  }
  if (!WiFi.isConnected()) {
    display_.showQuickLabelPrintResult(false, "PC OFFLINE");
    state_ = State::Idle;
    return;
  }
  pendingQuickLabelPrintId_ = clientConfig_.deviceId + String("-label-") + String(syncSequence_++);
  pendingQuickLabelPresetIndex_ = presetIndex;
  pendingQuickLabelRevision_ = quickLabelRevision_;
  state_ = State::PrintingLabel;
  display_.showPrinting(quickLabelPresets_[presetIndex - 1]);
  lastSyncAttempt_ = 0;
}

void HimsScanApp::persistContrast() {
  if (!deviceSettings_.saveContrast(lcdContrast_)) Serial.println("Could not save LCD contrast.");
}

DisplayConnectivityState HimsScanApp::connectivityState() const {
  if (!WiFi.isConnected()) {
    if (provisionedSettings_.wifiSsid.length() > 0 && powerMode_ == PowerMode::Normal) {
      return DisplayConnectivityState::WifiConnecting;
    }
    return DisplayConnectivityState::Offline;
  }
  if (!pcOnline_) return DisplayConnectivityState::PcSearching;
  if (!outbox_.empty() || pendingResultAck_.length() > 0 || activeLookupId_.length() > 0 ||
      pendingQuickLabelPrintId_.length() > 0) {
    return DisplayConnectivityState::Syncing;
  }
  return DisplayConnectivityState::Online;
}

void HimsScanApp::updateConnectivityDisplay() {
  const bool wifiOnline = WiFi.isConnected();
  display_.setConnectivity(connectivityState(), wifiOnline, pcOnline_, outbox_.size());
  if (state_ == State::AwaitQuantity && pendingScanType_ == PendingScanType::HimsQuantity &&
      activeLookupId_.length() > 0) {
    display_.updateQuantityItem(wifiOnline ? String("Looking up item") : String("Inventory item"));
  }
}

}  // namespace hims_scan
