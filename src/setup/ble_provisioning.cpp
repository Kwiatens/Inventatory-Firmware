// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// BLE-only first-use provisioning for Inventatory Scan R1.

#include "setup/ble_provisioning.h"

#include "InventatoryScanBleRequest.h"
#include "InventatoryScanSecurity.h"
#include "log/device_log.h"

#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLESecurity.h>

#include <esp_gap_ble_api.h>
#include <esp_system.h>

namespace inventatory_scan {

namespace {

constexpr char kServiceUuid[] = "d4d4f5b0-4c21-4a7e-a1a1-4b0db2d00001";
constexpr char kStatusUuid[] = "d4d4f5b0-4c21-4a7e-a1a1-4b0db2d00002";
constexpr char kRequestUuid[] = "d4d4f5b0-4c21-4a7e-a1a1-4b0db2d00003";
// The passkey changes after every attempt, since a reused passkey can leak information.
// Repeated failures lock pairing until a device keypress.
constexpr uint8_t kMaxFailedPairingAttempts = 3;
constexpr unsigned long kAdvertisingWindowMs = 5UL * 60UL * 1000UL;

// A six-digit passkey, uniform over 100000-999999. Rejection sampling avoids the modulo bias, and the
// caller draws it only while the radio runs: esp_random() is only pseudo-random with the radio off.
uint32_t newPasskey() {
  constexpr uint32_t kRange = 900000UL;
  constexpr uint32_t kLimit = UINT32_MAX - (UINT32_MAX % kRange);
  uint32_t value = esp_random();
  while (value >= kLimit) value = esp_random();
  return 100000UL + (value % kRange);
}

class ServerCallbacks final : public BLEServerCallbacks {
 public:
  explicit ServerCallbacks(BleProvisioning& owner) : owner_(owner) {}
  // Logs carry no secrets; they show where a pairing attempt ends.
  void onConnect(BLEServer*, esp_ble_gatts_cb_param_t*) override { deviceLog.println("BLE: client connected"); }
  void onDisconnect(BLEServer*, esp_ble_gatts_cb_param_t* param) override {
    deviceLog.printf("BLE: client disconnected, reason 0x%02x\r\n", static_cast<unsigned>(param->disconnect.reason));
  }
  void onDisconnect(BLEServer*) override {
    // Only records the event; service() restarts advertising from Core 1.
    owner_.onClientDisconnected();
  }
 private:
  BleProvisioning& owner_;
};

class SecurityCallbacks final : public BLESecurityCallbacks {
 public:
  explicit SecurityCallbacks(BleProvisioning& owner) : owner_(owner) {}
  uint32_t onPassKeyRequest() override { return owner_.pairingCode(); }
  void onPassKeyNotify(uint32_t) override {}
  bool onSecurityRequest() override { return true; }
  bool onConfirmPIN(uint32_t pin) override { return pin == owner_.pairingCode(); }
  void onAuthenticationComplete(esp_ble_auth_cmpl_t result) override {
    if (result.success) {
      deviceLog.println("BLE: pairing succeeded");
    } else {
      deviceLog.printf("BLE: pairing failed, reason 0x%02x\r\n", static_cast<unsigned>(result.fail_reason));
    }
    owner_.onAuthenticationResult(result.success);
  }
 private:
  BleProvisioning& owner_;
};

class RequestCallbacks final : public BLECharacteristicCallbacks {
 public:
  explicit RequestCallbacks(BleProvisioning& owner) : owner_(owner) {}
  void onWrite(BLECharacteristic* characteristic) override {
    owner_.handleEncryptedWrite(characteristic->getValue());
  }
 private:
  BleProvisioning& owner_;
};

}  // namespace

bool BleProvisioning::begin(const String& deviceId) {
  if (active_) return true;
  // A second start in one boot would run on a released stack and duplicate the advertised UUID.
  if (startedOnce_) return false;
  startedOnce_ = true;
  if (requestMutex_ == nullptr) requestMutex_ = xSemaphoreCreateMutex();
  if (requestMutex_ == nullptr) return false;
  locked_ = false;
  failedAttempts_ = 0;
  authFailed_ = false;
  clientDisconnected_ = false;
  const auto suffix = deviceId.length() > 4 ? deviceId.substring(deviceId.length() - 4) : deviceId;
  const String advertisedName = String("Inventatory Setup ") + suffix;
  BLEDevice::init(advertisedName.c_str());
  // Drawn after init, so the Bluetooth radio feeds the hardware random number generator.
  pairingCode_ = newPasskey();
  // Drop earlier bonds: a bonded peer could reconnect without the passkey on screen.
  {
    int bondCount = esp_ble_get_bond_device_num();
    if (bondCount > 0) {
      auto* bonded = static_cast<esp_ble_bond_dev_t*>(malloc(sizeof(esp_ble_bond_dev_t) * bondCount));
      if (bonded != nullptr) {
        if (esp_ble_get_bond_device_list(&bondCount, bonded) == ESP_OK) {
          for (int i = 0; i < bondCount; ++i) esp_ble_remove_bond_device(bonded[i].bd_addr);
        }
        free(bonded);
      }
    }
  }
  // Raise the MTU so the request (SSID, password, token) fits one write, avoiding ambiguous
  // prepared-write completion on desktop BLE stacks.
  BLEDevice::setMTU(247);
  BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT_MITM);
  // The stack keeps these pointers, so they are static; a new set per session would only leak.
  static SecurityCallbacks securityCallbacks(*this);
  static ServerCallbacks serverCallbacks(*this);
  static RequestCallbacks requestCallbacks(*this);
  static BLESecurity security;
  BLEDevice::setSecurityCallbacks(&securityCallbacks);
  security_ = &security;
  security.setStaticPIN(pairingCode_.load());
  security.setCapability(ESP_IO_CAP_OUT);
  security.setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM);
  security.setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  security.setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  security.setKeySize(16);
  // SC with MITM is otherwise only requested: a central without LE Secure Connections would fall back to
  // legacy passkey pairing, whose key a passive sniffer recovers, and read the Wi-Fi password and token.
  uint8_t onlySecureConnections = ESP_BLE_ONLY_ACCEPT_SPECIFIED_AUTH_ENABLE;
  esp_ble_gap_set_security_param(ESP_BLE_SM_ONLY_ACCEPT_SPECIFIED_SEC_AUTH, &onlySecureConnections,
                                 sizeof(onlySecureConnections));

  server_ = BLEDevice::createServer();
  server_->setCallbacks(&serverCallbacks);
  auto* service = server_->createService(kServiceUuid);
  status_ = service->createCharacteristic(kStatusUuid, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  request_ = service->createCharacteristic(kRequestUuid, BLECharacteristic::PROPERTY_WRITE);
  const auto secureRead = static_cast<esp_gatt_perm_t>(ESP_GATT_PERM_READ_ENC_MITM);
  const auto secureWrite = static_cast<esp_gatt_perm_t>(ESP_GATT_PERM_WRITE_ENC_MITM);
  status_->setAccessPermissions(secureRead);
  request_->setAccessPermissions(secureWrite);
  status_->addDescriptor(new BLE2902());
  request_->setCallbacks(&requestCallbacks);
  service->start();
  server_->getAdvertising()->addServiceUUID(kServiceUuid);
  // The advertisement holds flags and the service UUID (21 of 31 bytes). The default scan response
  // would repeat the UUID and overflow 31 bytes, so the scan response carries only the name.
  BLEAdvertisementData scanResponse;
  scanResponse.setName(advertisedName.c_str());
  server_->getAdvertising()->setScanResponseData(scanResponse);
  server_->getAdvertising()->setScanResponse(true);
  active_ = true;
  advertisingStartedAt_ = millis();
  server_->startAdvertising();
  publishStatus("READY");
  return true;
}

void BleProvisioning::end() {
  if (!active_) return;
  active_ = false;
  BLEDevice::stopAdvertising();
  // deinit(true) frees Bluetooth memory; begin() does not run again this boot (see startedOnce_).
  BLEDevice::deinit(true);
  server_ = nullptr;
  security_ = nullptr;
  status_ = nullptr;
  request_ = nullptr;
  if (requestMutex_ != nullptr && xSemaphoreTake(requestMutex_, portMAX_DELAY) == pdTRUE) {
    requestPending_ = false;
    pending_ = {};
    xSemaphoreGive(requestMutex_);
  } else {
    requestPending_ = false;
    pending_ = {};
  }
}

void BleProvisioning::rotatePasskey() {
  if (security_ == nullptr) return;
  pairingCode_ = newPasskey();
  // Update only the passkey: BLESecurity::setStaticPIN() would also reset the IO capability,
  // key size and authentication mode (to SC without MITM).
  uint32_t passkey = pairingCode_.load();
  esp_ble_gap_set_security_param(ESP_BLE_SM_SET_STATIC_PASSKEY, &passkey, sizeof(passkey));
  passkeyChanged_ = true;
}

void BleProvisioning::lockOut() {
  locked_ = true;
  passkeyChanged_ = true;
  BLEDevice::stopAdvertising();
  if (server_ != nullptr && server_->getConnectedCount() > 0) server_->disconnect(server_->getConnId());
}

void BleProvisioning::service() {
  if (!active_) return;
  flushStatus();
  const bool failed = authFailed_.exchange(false);
  const bool disconnected = clientDisconnected_.exchange(false);
  if (locked_) return;
  if (failed || disconnected) rotatePasskey();
  // Keep setup discoverable after a failed or cancelled attempt.
  if (disconnected && server_ != nullptr) server_->startAdvertising();
  if (failed && ++failedAttempts_ >= kMaxFailedPairingAttempts) {
    lockOut();
    return;
  }
  // The window closes only with no client connected, so a pairing in progress is never cut off.
  if (server_ != nullptr && server_->getConnectedCount() > 0) {
    advertisingStartedAt_ = millis();
  } else if (millis() - advertisingStartedAt_ > kAdvertisingWindowMs) {
    lockOut();
  }
}

void BleProvisioning::rearm() {
  if (!active_ || !locked_) return;
  failedAttempts_ = 0;
  rotatePasskey();
  locked_ = false;
  advertisingStartedAt_ = millis();
  if (server_ != nullptr) server_->startAdvertising();
  passkeyChanged_ = true;
}

bool BleProvisioning::takeRequest(ProvisionedSettings& settings) {
  if (requestMutex_ == nullptr || xSemaphoreTake(requestMutex_, portMAX_DELAY) != pdTRUE) return false;
  if (!requestPending_) {
    xSemaphoreGive(requestMutex_);
    return false;
  }
  settings = pending_;
  pending_ = {};
  requestPending_ = false;
  xSemaphoreGive(requestMutex_);
  return true;
}

void BleProvisioning::setResult(bool success, const String& publicMessage) {
  publishStatus(success ? String("SUCCESS ") + publicMessage : String("FAILED ") + publicMessage);
}

void BleProvisioning::handleEncryptedWrite(const std::string& value) {
  // BLE callback context: only records the outcome; service() updates the status on Core 1.
  ProvisionedSettings parsed;
  if (!parseRequest(value, parsed)) {
    queueStatus("INVALID REQUEST");
    return;
  }
  if (requestMutex_ == nullptr || xSemaphoreTake(requestMutex_, portMAX_DELAY) != pdTRUE) {
    queueStatus("FAILED INTERNAL");
    return;
  }
  pending_ = parsed;
  requestPending_ = true;
  xSemaphoreGive(requestMutex_);
  queueStatus("CONNECTING");
}

bool BleProvisioning::parseRequest(const std::string& value, ProvisionedSettings& settings) const {
  // Version, then SSID, password and token lengths, then the bytes (InventatoryScanBleRequest.h).
  return parseProvisioningRequest(reinterpret_cast<const uint8_t*>(value.data()), value.size(), settings);
}

void BleProvisioning::queueStatus(const String& publicMessage) {
  if (requestMutex_ == nullptr || xSemaphoreTake(requestMutex_, portMAX_DELAY) != pdTRUE) return;
  pendingStatus_ = publicMessage;
  statusPending_ = true;
  xSemaphoreGive(requestMutex_);
}

void BleProvisioning::flushStatus() {
  String message;
  if (requestMutex_ == nullptr || xSemaphoreTake(requestMutex_, portMAX_DELAY) != pdTRUE) return;
  if (statusPending_) {
    message = pendingStatus_;
    statusPending_ = false;
  }
  xSemaphoreGive(requestMutex_);
  if (message.length() > 0) publishStatus(message);
}

void BleProvisioning::publishStatus(const String& publicMessage) {
  if (status_ == nullptr) return;
  status_->setValue(publicMessage.c_str());
  status_->notify();
}

}  // namespace inventatory_scan
