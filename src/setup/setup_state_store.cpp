// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Inventatory Scanner R1 persistent provisioning state.

#include "setup/setup_state_store.h"

#include "InventatoryScanSecurity.h"

namespace inventatory_scan {

namespace {

constexpr char kSetupNamespace[] = "inv_setup";
static_assert(sizeof(kSetupNamespace) <= 16, "NVS namespace exceeds ESP32's 15-character limit");
// The ESP-IDF Wi-Fi driver's namespace in the default NVS partition.
constexpr char kWifiDriverNamespace[] = "nvs.net80211";

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
  // Mark incomplete first: a power loss can only return to setup mode,
  // never make a partial set look valid.
  if (!prefs_.putBool("complete", false)) return false;
  // putString() returns strlen(value), so an empty password (open network) is accepted.
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
  // A missing counter on a complete record fails closed; defaulting to 0 would reuse
  // counters the desktop has seen.
  if (!prefs_.isKey("request_counter")) return false;
  if (!counterBlockValid_ || counterNext_ >= counterCeiling_) {
    // The persisted value is the previous block's ceiling; counters at or below it may be sent already.
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
  const bool cleared = prefs_.clear();
  // Best effort: the record above decides whether pairing was cleared.
  clearWifiDriverCopy();
  return cleared;
}

bool SetupStateStore::clearWifiDriverCopy() {
  Preferences driver;
  if (!driver.begin(kWifiDriverNamespace, false)) return false;
  const bool cleared = driver.clear();
  driver.end();
  return cleared;
}

}  // namespace inventatory_scan
