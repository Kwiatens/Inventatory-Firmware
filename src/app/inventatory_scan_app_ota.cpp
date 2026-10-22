// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#include "app/inventatory_scan_app.h"

#include "InventatoryScanCore.h"
#include "InventatoryScanErrorCodes.h"
#include "InventatoryScanSecurity.h"
#include "InventatoryScanUiPolicy.h"
#include "config/config.h"
#include "config/version.h"
#include "log/device_log.h"

#include <ArduinoOTA.h>
#include <WiFi.h>

namespace inventatory_scan {

namespace {
// How long an OTA request waits for Wi-Fi and an uploader before giving up.
constexpr unsigned long kOtaWaitTimeoutMs = 120000UL;
}  // namespace

void InventatoryScanApp::requestOtaUpdate() {
  if (otaRequested_) {
    deviceLog.println("OTA update already requested.");
    return;
  }
  if (setupMode_ || provisionedSettings_.wifiSsid.length() == 0) {
    deviceLog.println("OTA update ignored because setup is incomplete.");
    return;
  }
  // A build from the example config would otherwise accept any upload signed with the public placeholder.
  if (!isUsableOtaPassword(OTA_PASSWORD)) {
    deviceLog.println("OTA update refused: set a private OTA password of 12 or more characters in config.h.");
    display_.showLocalFailure(localFailure("local-ota", kUpdateFailedCode, "OTA password not set"));
    return;
  }

  resetPending();
  otaRequested_ = true;
  otaActive_ = false;
  otaUploadStarted_ = false;
  otaRequestedAt_ = millis();
  networkPowerOffRequested_ = false;
  // An acknowledgement left from an earlier pause must not satisfy this request, so clear it first.
  networkPauseAcknowledged_ = false;
  networkPaused_ = true;
  scanner_.flushInput();
  if (powerMode_ == PowerMode::Standby) {
    deviceLog.println("Waking standby path for OTA update.");
  }
  deviceLog.println("OTA update requested. Normal scanning is paused until the update finishes.");
  display_.showOta("CONNECTING", 0);
  sendDebugMessage("ota requested", "warn");
}

bool InventatoryScanApp::serviceOta() {
  if (otaEndPending_) {
    otaEndPending_ = false;
    ArduinoOTA.end();
  }
  if (!otaRequested_) {
    return false;
  }

  if (provisionedSettings_.wifiSsid.length() == 0) {
    deviceLog.println("OTA request ignored because setup is incomplete.");
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
      deviceLog.println("OTA service failed to start; resuming normal operation.");
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
    deviceLog.println("OTA request ignored because setup is incomplete.");
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

  deviceLog.println("Connecting for OTA to saved local Wi-Fi.");
  WiFi.begin(provisionedSettings_.wifiSsid.c_str(), provisionedSettings_.wifiPassword.c_str());
  return false;
}

bool InventatoryScanApp::startOtaService() {
  if (!isUsableOtaPassword(OTA_PASSWORD)) return false;
  WiFi.setSleep(false);
  ArduinoOTA.setHostname(clientConfig_.deviceId.c_str());
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([this]() {
    otaActive_ = true;
    otaUploadStarted_ = true;
    deviceLog.println("OTA start: flashing new firmware.");
    otaDisplayedPercent_ = 0;
    display_.showOta("UPDATING", 0);
    display_.tick();
  });
  ArduinoOTA.onEnd([this]() {
    deviceLog.println();
    deviceLog.println("OTA complete; restarting device.");
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
    deviceLog.printf("OTA progress: %u%%\r", percent);
    // The upload runs inside ArduinoOTA.handle(), so loop() cannot tick the display.
    // Draw from here; tick() redraws only when the percent changed.
    if (static_cast<int>(percent) != otaDisplayedPercent_) {
      otaDisplayedPercent_ = static_cast<int>(percent);
      display_.showOta("UPDATING", otaDisplayedPercent_);
    }
    display_.tick();
  });
  ArduinoOTA.onError([this](ota_error_t error) {
    deviceLog.print("OTA error: ");
    switch (error) {
      case OTA_AUTH_ERROR:
        deviceLog.println("authentication failed");
        break;
      case OTA_BEGIN_ERROR:
        deviceLog.println("begin failed");
        break;
      case OTA_CONNECT_ERROR:
        deviceLog.println("connection failed");
        break;
      case OTA_RECEIVE_ERROR:
        deviceLog.println("receive failed");
        break;
      case OTA_END_ERROR:
        deviceLog.println("end failed");
        break;
      default:
        deviceLog.println("unknown");
        break;
    }
    recoverFromOtaFailure();
    otaEndPending_ = true;
    display_.showResult(localFailure("local-ota", kUpdateFailedCode, "Update failed"));
  });

  ArduinoOTA.begin();
  otaActive_ = true;
  deviceLog.print("OTA update ready for ");
  deviceLog.print(clientConfig_.deviceId.c_str());
  deviceLog.print(" at ");
  deviceLog.println(WiFi.localIP());
  sendDebugMessage(String("ota ready at ") + WiFi.localIP().toString(), "warn");
  return true;
}

void InventatoryScanApp::abandonOtaWait(const char* reason, bool showFailure) {
  deviceLog.println(reason);
  // No upload was received, so the service can be stopped here, unlike inside a callback.
  if (otaActive_) ArduinoOTA.end();
  recoverFromOtaFailure();
  if (showFailure) {
    display_.showLocalFailure(localFailure("local-ota", kNoUpdateCode, "No update received"));
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