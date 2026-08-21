// BLE-only first-use provisioning for Inventatory Scan R1.

#include "setup/ble_provisioning.h"

#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLESecurity.h>

#include <esp_system.h>

namespace inventatory_scan {

namespace {

constexpr char kServiceUuid[] = "d4d4f5b0-4c21-4a7e-a1a1-4b0db2d00001";
constexpr char kStatusUuid[] = "d4d4f5b0-4c21-4a7e-a1a1-4b0db2d00002";
constexpr char kRequestUuid[] = "d4d4f5b0-4c21-4a7e-a1a1-4b0db2d00003";
constexpr uint8_t kProtocolVersion = 1;

class ServerCallbacks final : public BLEServerCallbacks {
 public:
  explicit ServerCallbacks(BleProvisioning& owner) : owner_(owner) {}
  void onDisconnect(BLEServer* server) override {
    // Keep first-use setup discoverable after a failed/cancelled attempt.
    if (owner_.active()) server->startAdvertising();
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
  void onAuthenticationComplete(esp_ble_auth_cmpl_t) override {}
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
  if (requestMutex_ == nullptr) requestMutex_ = xSemaphoreCreateMutex();
  if (requestMutex_ == nullptr) return false;
  pairingCode_ = 100000UL + (esp_random() % 900000UL);
  const auto suffix = deviceId.length() > 4 ? deviceId.substring(deviceId.length() - 4) : deviceId;
  BLEDevice::init(("Inventatory Setup " + suffix).c_str());
  // The provisioning payload contains the SSID, password, and a 32-byte
  // token. Negotiate enough ATT payload space to keep it a single write and
  // avoid ambiguous prepared-write completion on desktop BLE stacks.
  BLEDevice::setMTU(247);
  BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT_MITM);
  BLEDevice::setSecurityCallbacks(new SecurityCallbacks(*this));
  auto* security = new BLESecurity();
  security->setStaticPIN(pairingCode_);
  security->setCapability(ESP_IO_CAP_OUT);
  security->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
  security->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  security->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  security->setKeySize(16);

  server_ = BLEDevice::createServer();
  server_->setCallbacks(new ServerCallbacks(*this));
  auto* service = server_->createService(kServiceUuid);
  status_ = service->createCharacteristic(kStatusUuid, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  request_ = service->createCharacteristic(kRequestUuid, BLECharacteristic::PROPERTY_WRITE);
  const auto secureRead = static_cast<esp_gatt_perm_t>(ESP_GATT_PERM_READ_ENC_MITM);
  const auto secureWrite = static_cast<esp_gatt_perm_t>(ESP_GATT_PERM_WRITE_ENC_MITM);
  status_->setAccessPermissions(secureRead);
  request_->setAccessPermissions(secureWrite);
  status_->addDescriptor(new BLE2902());
  request_->setCallbacks(new RequestCallbacks(*this));
  service->start();
  server_->getAdvertising()->addServiceUUID(kServiceUuid);
  server_->getAdvertising()->setScanResponse(true);
  active_ = true;
  server_->startAdvertising();
  publishStatus("READY");
  return true;
}

void BleProvisioning::end() {
  if (!active_) return;
  active_ = false;
  BLEDevice::stopAdvertising();
  BLEDevice::deinit(true);
  server_ = nullptr;
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
  ProvisionedSettings parsed;
  if (!parseRequest(value, parsed)) {
    publishStatus("INVALID REQUEST");
    return;
  }
  if (requestMutex_ == nullptr || xSemaphoreTake(requestMutex_, portMAX_DELAY) != pdTRUE) {
    publishStatus("FAILED INTERNAL");
    return;
  }
  pending_ = parsed;
  requestPending_ = true;
  xSemaphoreGive(requestMutex_);
  publishStatus("CONNECTING");
}

bool BleProvisioning::parseRequest(const std::string& value, ProvisionedSettings& settings) const {
  // Format: version,SSID length,password length,token length,then the exact bytes.
  // It deliberately contains no JSON, delimiters, logging, or secret echo path.
  if (value.size() < 4 || static_cast<uint8_t>(value[0]) != kProtocolVersion) return false;
  const auto ssidLength = static_cast<uint8_t>(value[1]);
  const auto passwordLength = static_cast<uint8_t>(value[2]);
  const auto tokenLength = static_cast<uint8_t>(value[3]);
  const size_t expected = 4U + ssidLength + passwordLength + tokenLength;
  if (ssidLength == 0 || ssidLength > 32 || passwordLength > 63 || tokenLength < 32 || tokenLength > 128 ||
      value.size() != expected) return false;
  size_t offset = 4;
  settings.wifiSsid = String(value.data() + offset).substring(0, ssidLength);
  offset += ssidLength;
  settings.wifiPassword = String(value.data() + offset).substring(0, passwordLength);
  offset += passwordLength;
  settings.deviceToken = String(value.data() + offset).substring(0, tokenLength);
  return settings.wifiSsid.length() == ssidLength && settings.wifiPassword.length() == passwordLength &&
         settings.deviceToken.length() == tokenLength;
}

void BleProvisioning::publishStatus(const String& publicMessage) {
  if (status_ == nullptr) return;
  status_->setValue(publicMessage.c_str());
  status_->notify();
}

}  // namespace inventatory_scan
