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

// Stores only validated provisioning data.  Inventory events intentionally use
// their own NVS namespace, so resetting setup never destroys the outbox.
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

 private:
  static constexpr uint32_t kMagic = 0x49534554UL;  // "ISET"
  static constexpr uint16_t kVersion = 1;

  mutable Preferences prefs_;
};

}  // namespace inventatory_scan
