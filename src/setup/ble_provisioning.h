// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// BLE-only first-use provisioning for Inventatory Scanner R1.
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
  // Set after too many failed pairings or the advertising window closes; a device keypress re-arms it.
  bool locked() const { return locked_; }
  // Core 1 housekeeping: closes the advertising window.
  void service();
  // New advertising window with a fresh passkey; called on a device keypress.
  void rearm();
  // True once after the passkey changes, so Core 1 redraws the LCD.
  bool takePasskeyChanged() { return passkeyChanged_.exchange(false); }
  // Called from BLE callbacks on the Bluetooth task; they only record the event. service() acts on it
  // from Core 1, never concurrently with end().
  void onAuthenticationResult(bool success) { if (!success) authFailed_ = true; }
  void onClientDisconnected() { clientDisconnected_ = true; }
  bool takeRequest(ProvisionedSettings& settings);
  void setResult(bool success, const String& publicMessage);
  void handleEncryptedWrite(const std::string& value);

 private:
  void rotatePasskey();
  void lockOut();
  bool parseRequest(const std::string& value, ProvisionedSettings& settings) const;
  // BLE callback status text is queued here and published from Core 1.
  void queueStatus(const String& publicMessage);
  void flushStatus();
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
  // Guarded by requestMutex_: the request and the unpublished status text.
  bool requestPending_ = false;
  ProvisionedSettings pending_;
  bool statusPending_ = false;
  String pendingStatus_;
  SemaphoreHandle_t requestMutex_ = nullptr;
  // end() frees the Bluetooth memory and the BLE library cannot restart in one boot, so begin()
  // runs at most once per boot.
  bool startedOnce_ = false;
};

}  // namespace inventatory_scan
