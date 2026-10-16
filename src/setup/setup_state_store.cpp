// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Inventatory Scan R1 persistent provisioning state.

#include "setup/setup_state_store.h"

#include "InventatoryScanSecurity.h"

namespace inventatory_scan {

namespace {

// ESP32 NVS namespaces are limited to 15 characters plus the terminator.
constexpr char kSetupNamespace[] = "inv_setup";
static_assert(sizeof(kSetupNamespace) <= 16, "NVS namespace exceeds ESP32's 15-character limit");

}  // namespace

bool SetupStateStore::begin() { return prefs_.begin(kSetupNamespace, false); }

bool SetupStateStore::complete() const {
  return prefs_.getUInt("magic", 0) == kMagic &&
         prefs_.getUShort("version", 0) == kVersion &&
         prefs_.getBool("complete", false);
}

bool SetupStateStore::load(ProvisionedSettings& settings) const {
  settings = {};
  if (!complete()) return false;
  settings.wifiSsid = prefs_.getString("ssid", "");
  settings.wifiPassword = prefs_.getString("password", "");
  settings.deviceToken = prefs_.getString("token", "");
  return settings.wifiSsid.length() > 0 && isValidTransportToken(settings.deviceToken);
}

bool SetupStateStore::savePending(const ProvisionedSettings& settings) {
  if (settings.wifiSsid.length() == 0 || settings.wifiSsid.length() > 32 ||
      settings.wifiPassword.length() > 63 || !isValidTransportToken(settings.deviceToken)) {
    return false;
  }

  counterBlockValid_ = false;
  // Mark incomplete first.  A power loss can therefore only return the R1 to
  // setup mode; it can never make a partial credential set look valid.
  if (!prefs_.putBool("complete", false)) return false;
  // Arduino ESP32 Preferences returns strlen(value) for putString(), which
  // correctly permits an intentionally empty password for an open network.
  if (prefs_.putString("ssid", settings.wifiSsid) != settings.wifiSsid.length() ||
      prefs_.putString("password", settings.wifiPassword) != settings.wifiPassword.length() ||
      prefs_.putString("token", settings.deviceToken) != settings.deviceToken.length() ||
      prefs_.putULong64("request_counter", 0) != sizeof(uint64_t) ||
      !prefs_.putUInt("magic", kMagic) || !prefs_.putUShort("version", kVersion)) {
    return false;
  }
  return true;
}

bool SetupStateStore::commit() {
  if (prefs_.getUInt("magic", 0) != kMagic || prefs_.getUShort("version", 0) != kVersion ||
      prefs_.getString("ssid", "").length() == 0 || !isValidTransportToken(prefs_.getString("token", ""))) {
    return false;
  }
  return prefs_.putBool("complete", true);
}

bool SetupStateStore::save(const ProvisionedSettings& settings) {
  return savePending(settings) && commit();
}

bool SetupStateStore::reserveTransportCounter(uint64_t& counter) {
  if (!complete()) return false;
  // A missing counter on a complete record must fail closed.  Defaulting to 0
  // would restart counters at 1 and reuse values the desktop has already seen.
  if (!prefs_.isKey("request_counter")) return false;
  if (!counterBlockValid_ || counterNext_ >= counterCeiling_) {
    // The persisted value is the ceiling of the previous block, so every
    // counter at or below it may already have been transmitted.
    const std::uint64_t base = counterBlockValid_ ? counterCeiling_ : prefs_.getULong64("request_counter", 0);
    if (base > UINT64_MAX - kCounterBlock) return false;
    const std::uint64_t ceiling = base + kCounterBlock;
    if (prefs_.putULong64("request_counter", ceiling) != sizeof(ceiling)) return false;
    counterNext_ = base;
    counterCeiling_ = ceiling;
    counterBlockValid_ = true;
  }
  counter = ++counterNext_;
  return true;
}

bool SetupStateStore::clear() {
  counterBlockValid_ = false;
  return prefs_.clear();
}

}  // namespace inventatory_scan
