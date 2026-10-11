// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Inventatory Scan R1 application controller implementation.

#include "app/inventatory_scan_app.h"

#include "InventatoryScanCore.h"
#include "InventatoryScanUiPolicy.h"
#include "config/config.h"
#include "config/version.h"

#include <WiFi.h>

namespace inventatory_scan {

namespace {

// ESP32 NVS namespaces are limited to 15 characters plus the terminator.
constexpr char kOutboxNamespace[] = "inv_outbox";
static_assert(sizeof(kOutboxNamespace) <= 16, "NVS namespace exceeds ESP32's 15-character limit");

}  // namespace

bool InventatoryScanApp::begin() {
  Serial.println("Inventatory Scan R1 booting");
  display_.begin();
  display_.playBootIntro();
  keypadInit();
  battery_.begin();
  Serial.printf("Battery %lu mV\r\n", static_cast<unsigned long>(battery_.readMillivolts()));
  if (!scanner_.begin(9600)) {
    Serial.println("Scanner initialization failed; verify the GM65 connection and profile.");
    display_.showBoot("Scanner error");
    display_.tick();
    delay(2000);
  }
  scanner_.flushInput();
  display_.showBoot({});
  display_.tick();
  powerMode_ = PowerMode::Normal;
  otaRequested_ = false;
  otaActive_ = false;
  lastSyncAttempt_ = 0;
  bootNonce_ = esp_random() & 0xFFFFFFUL;
  syncSequence_ = 1;
  lookupSequence_ = 1;

  clientConfig_.deviceId = DEVICE_ID;
  clientConfig_.fallbackHost = FALLBACK_HOST;
  clientConfig_.fallbackPort = FALLBACK_PORT;

  if (!outbox_.begin(kOutboxNamespace, DEVICE_ID)) {
    Serial.println("Outbox storage unavailable; scans cannot be queued until NVS recovers.");
  } else {
    if (!outbox_.load()) {
      Serial.println("Outbox storage was reset; queued events could not be restored.");
    }
    Serial.printf("Outbox ready: %u queued, %u free NVS entries.\r\n", static_cast<unsigned>(outbox_.size()),
                  static_cast<unsigned>(outbox_.freeStorageEntries()));
  }
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
      display_.showBoot("Bluetooth error");
      display_.tick();
      return false;
    }
    display_.showSetup(provisioning_.pairingCode());
    display_.tick();
    return true;
  }
  if (beginNormalRuntime(provisionedSettings_)) return true;
  Serial.println("Normal runtime initialization failed; returning to BLE setup");
  return enterSetupAfterRuntimeFailure();
}

void InventatoryScanApp::loop() {
  if (setupMode_) {
    InventatoryKeyEvent event;
    while (keypadPoll(event)) handleKey(event);
    serviceProvisioning();
    display_.tick();
    delay(10);
    return;
  }
  updateConnectivityDisplay();
  display_.tick();
  InventatoryKeyEvent event;
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

bool InventatoryScanApp::beginNormalRuntime(const ProvisionedSettings& settings) {
  provisionedSettings_ = settings;
  otaRequested_ = false;
  otaActive_ = false;
  clientConfig_.token = settings.deviceToken;
  clientConfig_.transportCounterStore = &setupStore_;

  if (networkTask_.load() == nullptr) {
    networkInitializationComplete_ = false;
    networkInitializationSucceeded_ = false;
    networkStarted_ = false;
    wifiStarted_ = false;
    networkPauseAcknowledged_ = false;
    networkPowerOffRequested_ = false;
    networkPaused_ = false;
    if (!startNetworkWorker()) return false;
  }

  const auto startupDeadline = millis() + 1000UL;
  while (networkTask_.load() != nullptr && !networkInitializationComplete_ &&
         static_cast<long>(startupDeadline - millis()) > 0) {
    delay(1);
  }
  if (!networkInitializationComplete_ || !networkInitializationSucceeded_ || networkTask_.load() == nullptr) {
    return false;
  }

  const auto readyDeadline = millis() + 1000UL;
  while (networkTask_.load() != nullptr && !networkStarted_ &&
         static_cast<long>(readyDeadline - millis()) > 0) {
    delay(1);
  }
  if (!networkStarted_ || networkTask_.load() == nullptr) return false;

  if (networkPaused_) {
    const auto pauseDeadline = millis() + 1000UL;
    while (networkTask_.load() != nullptr && !networkPauseAcknowledged_ &&
           static_cast<long>(pauseDeadline - millis()) > 0) {
      delay(1);
    }
    if (networkTask_.load() != nullptr && !networkPauseAcknowledged_) return false;

    networkPowerOffRequested_ = false;
    networkPaused_ = false;
    const auto resumeDeadline = millis() + 1000UL;
    while (networkTask_.load() != nullptr && networkPauseAcknowledged_ &&
           static_cast<long>(resumeDeadline - millis()) > 0) {
      delay(1);
    }
    if (networkTask_.load() == nullptr || networkPauseAcknowledged_) return false;
  }

  sendDebugMessage("boot");
  logState("boot");
  updateConnectivityDisplay();
  display_.showReady();
  display_.tick();
  return true;
}

bool InventatoryScanApp::enterSetupAfterRuntimeFailure() {
  setupMode_ = true;
  if (!pauseNetworkForSetupTransition()) {
    setupRestartPending_ = true;
    display_.showBoot("Network busy");
    display_.tick();
    return true;
  }

  setupRestartPending_ = false;
  if (!provisioning_.begin(clientConfig_.deviceId)) {
    Serial.println("BLE provisioning could not restart after runtime failure");
    display_.showBoot("Bluetooth error");
    display_.tick();
    return false;
  }
  display_.showSetup(provisioning_.pairingCode(), "NETWORK ERROR");
  display_.tick();
  return true;
}

void InventatoryScanApp::serviceProvisioning() {
  if (setupRestartPending_) {
    enterSetupAfterRuntimeFailure();
    return;
  }

  provisioning_.service();
  if (provisioning_.takePasskeyChanged() && !setupWifiConnecting_) {
    display_.showSetup(provisioning_.pairingCode(),
                       provisioning_.locked() ? "PAIRING LOCKED" : "BLE READY");
  }

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
    // Keep the record incomplete until the normal runtime has actually been
    // started. A reset while BLE is being torn down must return to setup mode,
    // not boot into a credentialed but unreachable runtime.
    if (!setupStore_.savePending(provisionedSettings_)) {
      provisioning_.setResult(false, "SAVE FAILED");
      display_.showSetup(provisioning_.pairingCode(), "SAVE FAILED");
      WiFi.disconnect(false);
      return;
    }
    Serial.println("Provisioning: credentials staged");
    provisioning_.setResult(true, "WIFI OK");
    // Give the subscribed desktop client a scheduling window to receive the
    // terminal notification before BLE is deinitialized below.
    delay(300);
    provisioning_.end();
    setupMode_ = false;
    scanner_.flushInput();
    if (!beginNormalRuntime(provisionedSettings_) || !setupStore_.commit()) {
      Serial.println("Provisioning: normal runtime did not become ready; returning to BLE setup");
      enterSetupAfterRuntimeFailure();
    } else {
      Serial.println("Provisioning: normal runtime ready");
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

}  // namespace inventatory_scan
