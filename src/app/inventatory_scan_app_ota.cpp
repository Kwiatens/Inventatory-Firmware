// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Signed firmware update, pulled from the paired desktop (docs/ota-protocol.md). The network worker is
// paused for the whole update, so this file uses client_ from Core 1.
#include "app/inventatory_scan_app.h"

#include "InventatoryScanCore.h"
#include "InventatoryScanErrorCodes.h"
#include "InventatoryScanFirmwarePackage.h"
#include "InventatoryScanUiPolicy.h"
#include "config/ota_keys.h"
#include "config/version.h"
#include "log/device_log.h"
#include "ota/firmware_validity.h"

#include <Update.h>
#include <WiFi.h>
#include <esp_ota_ops.h>

namespace inventatory_scan {

namespace {
// How long an update request waits for Wi-Fi and the desktop's offer before giving up.
constexpr unsigned long kOtaWaitTimeoutMs = 120000UL;
// How long the confirmation screen waits for A or C; no answer declines.
constexpr unsigned long kOtaConsentTimeoutMs = 60000UL;
constexpr unsigned long kOtaOfferRetryMs = 2000UL;
// A failed chunk is asked for again this many times before the update stops.
constexpr int kOtaChunkAttempts = 3;
// Below this, the update waits for the charger. The A/B partitions keep a power cut safe; this only
// avoids a flat battery halfway.
constexpr int kOtaMinimumBatteryPercent = 30;

constexpr char kOfferPath[] = "/api/v1/device/firmware/offer";
constexpr char kChunkPath[] = "/api/v1/device/firmware/chunk";
constexpr char kStatusPath[] = "/api/v1/device/firmware/status";

// One line per failed try, so the log shows where the download stopped and how strong the Wi-Fi was.
void logChunkFailure(uint32_t offset, int attempt, int status, unsigned long elapsedMs, const char* reason) {
  deviceLog.print("OTA: chunk at byte ");
  deviceLog.print(offset);
  deviceLog.print(", try ");
  deviceLog.print(attempt);
  deviceLog.print(", status ");
  deviceLog.print(status);
  deviceLog.print(", ");
  deviceLog.print(reason);
  deviceLog.print(", ");
  deviceLog.print(elapsedMs);
  deviceLog.print(" ms, ");
  if (WiFi.status() == WL_CONNECTED) {
    deviceLog.print(WiFi.RSSI());
    deviceLog.println(" dBm");
  } else {
    deviceLog.println("Wi-Fi down");
  }
}

FirmwareTrustedKey gTrustedKeys[kFirmwareTrustedKeySlots];
bool gTrustedKeysLoaded = false;

const FirmwareTrustedKey* trustedKeys() {
  if (!gTrustedKeysLoaded) {
    gTrustedKeys[0] = firmwareTrustedKeyFromHex(kOtaTrustedKey0);
    gTrustedKeys[1] = firmwareTrustedKeyFromHex(kOtaTrustedKey1);
    gTrustedKeysLoaded = true;
  }
  return gTrustedKeys;
}

bool anyTrustedKey() {
  const auto* keys = trustedKeys();
  for (std::size_t slot = 0; slot < kFirmwareTrustedKeySlots; ++slot) {
    if (keys[slot].present) return true;
  }
  return false;
}
}  // namespace

void InventatoryScanApp::noteFirmwareOffer(bool offered) {
  // A build that trusts no key cannot install it, so it does not advertise it.
  display_.setUpdateAvailable(offered && !otaHintsMuted_ && anyTrustedKey());
}

void InventatoryScanApp::requestOtaUpdate() {
  if (otaRequested_) {
    deviceLog.println("OTA update already requested.");
    return;
  }
  if (setupMode_ || provisionedSettings_.wifiSsid.length() == 0) {
    deviceLog.println("OTA update ignored because setup is incomplete.");
    return;
  }
  if (!anyTrustedKey()) {
    // No key in src/config/ota_keys.h: updates come over USB only, and the desktop is not asked again.
    otaHintsMuted_ = true;
    deviceLog.println("OTA update refused: this build trusts no signing key (src/config/ota_keys.h).");
    display_.showLocalFailure(localFailure("local-ota", kUpdateFailedCode, "Updates off"));
    return;
  }

  resetPending();
  otaRequested_ = true;
  otaActive_ = false;
  otaUploadStarted_ = false;
  otaPhase_ = OtaPhase::Connecting;
  otaRequestedAt_ = millis();
  otaLastOfferAttempt_ = 0;
  // A request asks again after an earlier decline.
  otaHintsMuted_ = false;
  display_.setUpdateAvailable(false);
  networkPowerOffRequested_ = false;
  // An acknowledgement left from an earlier pause must not satisfy this request, so clear it first.
  networkPauseAcknowledged_ = false;
  networkPaused_ = true;
  scanner_.flushInput();
  if (powerMode_ == PowerMode::Standby) {
    deviceLog.println("Waking standby path for OTA update.");
  }
  deviceLog.println("OTA update requested.");
  display_.showOta("CONNECTING", 0);
}

bool InventatoryScanApp::serviceOta() {
  if (!otaRequested_) {
    return false;
  }

  if (provisionedSettings_.wifiSsid.length() == 0) {
    deviceLog.println("OTA request ignored because setup is incomplete.");
    recoverFromOtaFailure();
    return false;
  }

  if (otaPhase_ == OtaPhase::Connecting && millis() - otaRequestedAt_ > kOtaWaitTimeoutMs) {
    abandonOtaWait("OTA wait timed out; resuming normal operation.", true);
    return false;
  }

  if (otaPhase_ == OtaPhase::AwaitConsent) {
    if (millis() - otaConsentShownAt_ > kOtaConsentTimeoutMs) declineOtaUpdate("No answer; update not installed.");
    return otaRequested_;
  }

  if (!networkPauseAcknowledged_) return true;

  if (!ensureWiFiForOta()) {
    return true;
  }

  if (otaPhase_ == OtaPhase::Connecting) {
    if (otaLastOfferAttempt_ != 0 && millis() - otaLastOfferAttempt_ < kOtaOfferRetryMs) return true;
    otaLastOfferAttempt_ = millis();
    fetchOtaOffer();
    return otaRequested_;
  }

  if (otaPhase_ == OtaPhase::Installing) installOtaUpdate();
  return otaRequested_;
}

void InventatoryScanApp::fetchOtaOffer() {
  const auto body = buildFirmwareOfferRequestJson(kFirmwareProduct, kFirmwareHardware, kFirmwareVersion,
                                                  minimumFirmwareSecurityCounter(), trustedKeys(),
                                                  kFirmwareTrustedKeySlots);
  int status = 0;
  String response;
  if (!client_.postFirmware(kOfferPath, body, status, response)) {
    // No authenticated answer yet: the PC may still be starting. Retried until the wait times out.
    deviceLog.println("OTA offer: no answer from the PC yet.");
    return;
  }
  const auto offer = status == 200 ? parseFirmwareOfferJson(response) : FirmwareOfferResponse{};
  if (!offer.valid) {
    deviceLog.print("OTA offer refused, status ");
    deviceLog.println(status);
    failOtaUpdate(FirmwareCheck::BadHeader);
    return;
  }
  if (!offer.available) {
    otaHintsMuted_ = true;
    abandonOtaWait("The PC offers no update.", true);
    return;
  }

  FirmwareInstallPolicy policy;
  policy.product = kFirmwareProduct;
  policy.hardware = kFirmwareHardware;
  policy.minimumSecurityCounter = minimumFirmwareSecurityCounter();
  const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
  policy.partitionBytes = target != nullptr ? target->size : 0U;
  policy.keys = trustedKeys();
  policy.keyCount = kFirmwareTrustedKeySlots;
  const auto check = checkFirmwarePackagePrefix(offer.prefix, sizeof(offer.prefix), policy, otaHeader_);
  if (check != FirmwareCheck::Ok) {
    deviceLog.print("OTA offer refused: ");
    deviceLog.println(firmwareCheckReason(check));
    failOtaUpdate(check);
    return;
  }
  // The header is signed, so its version is safe to show and log.
  deviceLog.print("OTA offer verified: version ");
  deviceLog.println(otaHeader_.version);

  const int percent = batteryTracker_.percent();
  if (!batteryTracker_.charging() && (lowBatteryWarning_ || (percent >= 0 && percent < kOtaMinimumBatteryPercent))) {
    otaHintsMuted_ = true;
    sendOtaStatus("failed", "", "battery_low");
    deviceLog.println("OTA update refused: battery low and not charging.");
    recoverFromOtaFailure();
    // A warning, not a fault: no code (InventatoryScanErrorCodes.h, Battery).
    display_.showLocalFailure(localFailure("local-ota", "", "Charge to update"));
    return;
  }
  otaPhase_ = OtaPhase::AwaitConsent;
  otaConsentShownAt_ = millis();
  display_.showUpdateConfirm(String(otaHeader_.version));
}

void InventatoryScanApp::acceptOtaUpdate() {
  if (otaPhase_ != OtaPhase::AwaitConsent) return;
  otaPhase_ = OtaPhase::Installing;
  otaUploadStarted_ = true;
  otaDisplayedPercent_ = 0;
  display_.showOta("UPDATING", 0);
  display_.tick();
}

void InventatoryScanApp::declineOtaUpdate(const char* reason) {
  if (otaPhase_ != OtaPhase::AwaitConsent) return;
  deviceLog.println(reason);
  otaHintsMuted_ = true;
  sendOtaStatus("declined", "", "");
  finishOtaQuietly();
}

void InventatoryScanApp::installOtaUpdate() {
  otaActive_ = true;
  WiFi.setSleep(false);
  sendOtaStatus("installing", "", "");
  deviceLog.println("OTA start: downloading and flashing the new firmware.");

  FirmwareImageVerifier verifier;
  if (!verifier.begin(otaHeader_) || !Update.begin(otaHeader_.imageSize, U_FLASH)) {
    deviceLog.println("OTA: could not prepare the update partition.");
    failOtaUpdate(FirmwareCheck::DownloadFailed);
    return;
  }

  const auto downloadStarted = millis();
  uint32_t offset = 0;
  while (const uint32_t length = nextFirmwareChunkLength(otaHeader_.imageSize, offset)) {
    const auto request = buildFirmwareChunkRequestJson(otaHeader_.imageSha256, offset, length);
    String chunk;
    bool received = false;
    for (int attempt = 0; attempt < kOtaChunkAttempts && !received; ++attempt) {
      int status = 0;
      const auto attemptStarted = millis();
      received = client_.postFirmware(kChunkPath, request, status, chunk) && status == 200 &&
                 chunk.length() == length;
      if (!received) {
        logChunkFailure(offset, attempt + 1, status, millis() - attemptStarted,
                        status == 0 ? client_.lastFailure() : status == 200 ? "wrong length" : "refused");
      }
      if (!received && status != 0 && status != 200) break;  // the desktop refused; asking again will not help
    }
    // The bytes are counted into the hash before they are written; a chunk past the declared size is refused.
    auto* bytes = reinterpret_cast<uint8_t*>(const_cast<char*>(chunk.c_str()));
    if (!received || !verifier.accept(bytes, length) || Update.write(bytes, length) != length) {
      deviceLog.print("OTA: download stopped at byte ");
      deviceLog.println(offset);
      Update.abort();
      failOtaUpdate(FirmwareCheck::DownloadFailed);
      return;
    }
    offset += length;
    const int percent = static_cast<int>((static_cast<uint64_t>(offset) * 100U) / otaHeader_.imageSize);
    if (percent != otaDisplayedPercent_) {
      otaDisplayedPercent_ = percent;
      display_.showOta("UPDATING", percent);
    }
    display_.tick();
  }
  deviceLog.print("OTA: image downloaded in ");
  deviceLog.print((millis() - downloadStarted) / 1000UL);
  deviceLog.println(" s.");

  // The boot partition changes only in Update.end(), and only after the hash matched.
  const auto result = verifier.finish();
  if (result != FirmwareCheck::Ok) {
    Update.abort();
    deviceLog.print("OTA: image refused: ");
    deviceLog.println(firmwareCheckReason(result));
    failOtaUpdate(result);
    return;
  }
  if (!Update.end()) {
    deviceLog.print("OTA: the update partition did not accept the image, error ");
    deviceLog.println(Update.getError());
    failOtaUpdate(FirmwareCheck::DownloadFailed);
    return;
  }
  sendOtaStatus("installed", "", "");
  deviceLog.println("OTA complete; restarting device.");
  otaRequested_ = false;
  otaActive_ = false;
  display_.showOta("RESTARTING", 100);
  display_.tick();
  delay(250);
  ESP.restart();
}

void InventatoryScanApp::sendOtaStatus(const char* state, const char* error, const char* reason) {
  // Best effort: the desktop only shows it, and a lost status changes nothing here.
  int status = 0;
  String response;
  (void)client_.postFirmware(kStatusPath, buildFirmwareStatusJson(otaHeader_.imageSha256, state, error, reason),
                             status, response);
}

void InventatoryScanApp::failOtaUpdate(FirmwareCheck check) {
  otaHintsMuted_ = true;
  // A refused header leaves otaHeader_ cleared, so the status then names no image (all-zero hash).
  sendOtaStatus("failed", kUpdateFailedCode, firmwareCheckReason(check));
  recoverFromOtaFailure();
  display_.showLocalFailure(localFailure("local-ota", kUpdateFailedCode, firmwareCheckDisplayText(check)));
}

void InventatoryScanApp::finishOtaQuietly() {
  recoverFromOtaFailure();
  display_.showReady();
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

void InventatoryScanApp::abandonOtaWait(const char* reason, bool showFailure) {
  deviceLog.println(reason);
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
  otaPhase_ = OtaPhase::Idle;
  otaHeader_ = FirmwarePackageHeader{};
  networkPowerOffRequested_ = false;
  networkPaused_ = false;
  WiFi.setSleep(true);
  lastReconnectAttempt_ = 0;
  lastSyncAttempt_ = 0;
  state_ = State::Idle;
}

}  // namespace inventatory_scan
