// BLE-only first-use provisioning for Inventatory Scan R1.
#pragma once

#include "setup/setup_state_store.h"

#include <Arduino.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string>

class BLECharacteristic;
class BLESecurity;
class BLEServer;

namespace inventatory_scan {

class BleProvisioning {
 public:
  bool begin(const String& deviceId);
  void end();
  bool active() const { return active_; }
  uint32_t pairingCode() const { return pairingCode_.load(); }
  // True once pairing attempts failed too often or the advertising window
  // closed; only a physical keypress on the device re-arms it.
  bool locked() const { return locked_; }
  // Core 1 housekeeping: closes the advertising window.
  void service();
  // Starts a new advertising window with a fresh passkey (keypress on device).
  void rearm();
  // True once after the passkey changed, so Core 1 can redraw the LCD.
  bool takePasskeyChanged() { return passkeyChanged_.exchange(false); }
  // Called from BLE stack callbacks (Bluetooth task). They only record the event;
  // service() acts on it from Core 1 so the stack is never touched concurrently
  // with end().
  void onAuthenticationResult(bool success) { if (!success) authFailed_ = true; }
  void onClientDisconnected() { clientDisconnected_ = true; }
  bool takeRequest(ProvisionedSettings& settings);
  void setResult(bool success, const String& publicMessage);
  void handleEncryptedWrite(const std::string& value);

 private:
  void rotatePasskey();
  void lockOut();
  bool parseRequest(const std::string& value, ProvisionedSettings& settings) const;
  void publishStatus(const String& publicMessage);

  BLEServer* server_ = nullptr;
  BLECharacteristic* status_ = nullptr;
  BLECharacteristic* request_ = nullptr;
  bool active_ = false;
  BLESecurity* security_ = nullptr;
  std::atomic<uint32_t> pairingCode_{0};
  std::atomic<bool> locked_{false};
  std::atomic<bool> passkeyChanged_{false};
  std::atomic<bool> authFailed_{false};
  std::atomic<bool> clientDisconnected_{false};
  std::atomic<uint8_t> failedAttempts_{0};
  std::atomic<unsigned long> advertisingStartedAt_{0};
  bool requestPending_ = false;
  ProvisionedSettings pending_;
  SemaphoreHandle_t requestMutex_ = nullptr;
};

}  // namespace inventatory_scan
