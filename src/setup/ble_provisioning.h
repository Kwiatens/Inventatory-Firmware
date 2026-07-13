// BLE-only first-use provisioning for Inventatory Scan R1.
#pragma once

#include "setup/setup_state_store.h"

#include <Arduino.h>
#include <string>

class BLECharacteristic;
class BLEServer;

namespace hims_scan {

class BleProvisioning {
 public:
  bool begin(const String& deviceId);
  void end();
  bool active() const { return active_; }
  uint32_t pairingCode() const { return pairingCode_; }
  bool takeRequest(ProvisionedSettings& settings);
  void setResult(bool success, const String& publicMessage);
  void handleEncryptedWrite(const std::string& value);

 private:
  bool parseRequest(const std::string& value, ProvisionedSettings& settings) const;
  void publishStatus(const String& publicMessage);

  BLEServer* server_ = nullptr;
  BLECharacteristic* status_ = nullptr;
  BLECharacteristic* request_ = nullptr;
  bool active_ = false;
  uint32_t pairingCode_ = 0;
  bool requestPending_ = false;
  ProvisionedSettings pending_;
};

}  // namespace hims_scan
