// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Inventatory Scan R1 persistent provisioning state.
#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <cstdint>

namespace inventatory_scan {

struct ProvisionedSettings {
  String wifiSsid;
  String wifiPassword;
  String deviceToken;
};

// Stores validated data only. Inventory events use their own NVS namespace, so a setup reset
// never destroys the outbox.
class SetupStateStore {
 public:
  bool begin();
  bool load(ProvisionedSettings& settings) const;
  bool savePending(const ProvisionedSettings& settings);
  bool commit();
  bool save(const ProvisionedSettings& settings);
  bool reserveTransportCounter(std::uint64_t& counter);
  bool clear();
  bool complete() const;
  // Erases the Wi-Fi driver's own copy of the network settings (SSID, password, PMK) from NVS. The firmware
  // keeps those in this store only; the driver wrote its copy while it was left persistent.
  static bool clearWifiDriverCopy();

 private:
  static constexpr uint32_t kMagic = 0x49534554UL;  // "ISET"
  static constexpr uint16_t kVersion = 1;

  // Counters reserve NVS space in blocks: the persisted value is a ceiling over every counter handed
  // out. A reset skips counters but never reuses one, and NVS writes drop by the block size.
  static constexpr std::uint64_t kCounterBlock = 32;

  mutable Preferences prefs_;
  std::uint64_t counterNext_ = 0;     // last counter handed out
  std::uint64_t counterCeiling_ = 0;  // highest counter covered by the persisted ceiling
  bool counterBlockValid_ = false;
};

}  // namespace inventatory_scan
