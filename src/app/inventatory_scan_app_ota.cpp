// Inventatory Scan R1 application controller implementation.

#include "app/inventatory_scan_app.h"

#include "InventatoryScanCore.h"
#include "InventatoryScanUiPolicy.h"
#include "config/config.h"
#include "config/version.h"

#include <ArduinoOTA.h>
#include <WiFi.h>

namespace inventatory_scan {

namespace {
// How long an OTA request waits for Wi-Fi and an uploader before giving up.
constexpr unsigned long kOtaWaitTimeoutMs = 120000UL;
}  // namespace

void InventatoryScanApp::requestOtaUpdate() {
  if (otaRequested_) {
    Serial.println("OTA update already requested.");
    return;
  }
  if (setupMode_ || provisionedSettings_.wifiSsid.length() == 0) {
    Serial.println("OTA update ignored because setup is incomplete.");
    return;
  }

  resetPending();
  otaRequested_ = true;
  otaActive_ = false;
  otaUploadStarted_ = false;
  otaRequestedAt_ = millis();
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

bool InventatoryScanApp::serviceOta() {
  if (!otaRequested_) {
    return false;
  }

  if (provisionedSettings_.wifiSsid.length() == 0) {
    Serial.println("OTA request ignored because setup is incomplete.");
    recoverFromOtaFailure();
    return false;
  }

  if (!otaUploadStarted_ && millis() - otaRequestedAt_ > kOtaWaitTimeoutMs) {
    abandonOtaWait("OTA wait timed out; resuming normal operation.", true);
    return false;
  }

  if (!networkPauseAcknowledged_) return true;

  if (!ensureWiFiForOta()) {
    return true;
  }

  if (!otaActive_) {
    if (!startOtaService()) {
      Serial.println("OTA service failed to start; resuming normal operation.");
      recoverFromOtaFailure();
      return false;
    }
  }

  ArduinoOTA.handle();
  return true;
}

bool InventatoryScanApp::ensureWiFiForOta() {
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

bool InventatoryScanApp::startOtaService() {
  WiFi.setSleep(false);
  ArduinoOTA.setHostname(clientConfig_.deviceId.c_str());
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([this]() {
    otaActive_ = true;
    otaUploadStarted_ = true;
    Serial.println("OTA start: flashing new firmware.");
    otaDisplayedPercent_ = 0;
    display_.showOta("UPDATING", 0);
    display_.tick();
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
    // The upload runs to completion inside ArduinoOTA.handle() on this core, so
    // the loop does not tick the display until it ends; render from here. tick()
    // only redraws when the percentage or the dot's blink changed.
    if (static_cast<int>(percent) != otaDisplayedPercent_) {
      otaDisplayedPercent_ = static_cast<int>(percent);
      display_.showOta("UPDATING", otaDisplayedPercent_);
    }
    display_.tick();
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
    recoverFromOtaFailure();
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

void InventatoryScanApp::abandonOtaWait(const char* reason, bool showFailure) {
  Serial.println(reason);
  // The service was started but never received an upload, so it can be stopped
  // safely here (unlike from inside an ArduinoOTA callback).
  if (otaActive_) ArduinoOTA.end();
  recoverFromOtaFailure();
  if (showFailure) {
    SyncResult result;
    result.status = "failed";
    result.code = "OTA TIMEOUT";
    result.message = "No update received";
    result.resultId = "local-ota";
    result.eventId = "local-ota";
    display_.showLocalFailure(result);
  } else {
    display_.showReady();
  }
}

void InventatoryScanApp::recoverFromOtaFailure() {
  otaRequested_ = false;
  otaActive_ = false;
  otaUploadStarted_ = false;
  networkPowerOffRequested_ = false;
  networkPaused_ = false;
  WiFi.setSleep(true);
  lastReconnectAttempt_ = 0;
  lastSyncAttempt_ = 0;
  state_ = State::Idle;
}

}  // namespace inventatory_scan