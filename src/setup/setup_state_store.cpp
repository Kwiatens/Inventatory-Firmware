// Inventatory Scan R1 persistent provisioning state.

#include "setup/setup_state_store.h"

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
  return settings.wifiSsid.length() > 0 && settings.deviceToken.length() >= 32;
}

bool SetupStateStore::savePending(const ProvisionedSettings& settings) {
  if (settings.wifiSsid.length() == 0 || settings.wifiSsid.length() > 32 ||
      settings.wifiPassword.length() > 63 || settings.deviceToken.length() < 32 ||
      settings.deviceToken.length() > 128) {
    return false;
  }

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
  return prefs_.putBool("complete", false);
}

bool SetupStateStore::commit() {
  if (prefs_.getUInt("magic", 0) != kMagic || prefs_.getUShort("version", 0) != kVersion ||
      prefs_.getString("ssid", "").length() == 0 || prefs_.getString("token", "").length() < 32) {
    return false;
  }
  return prefs_.putBool("complete", true);
}

bool SetupStateStore::save(const ProvisionedSettings& settings) {
  return savePending(settings) && commit();
}

bool SetupStateStore::reserveTransportCounter(uint64_t& counter) {
  if (!complete()) return false;
  const auto previous = prefs_.getULong64("request_counter", 0);
  if (previous == UINT64_MAX) return false;
  counter = previous + 1U;
  return prefs_.putULong64("request_counter", counter) == sizeof(counter);
}

bool SetupStateStore::clear() {
  return prefs_.clear();
}

}  // namespace inventatory_scan
