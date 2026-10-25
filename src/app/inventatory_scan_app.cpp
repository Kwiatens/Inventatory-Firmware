// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#include "app/inventatory_scan_app.h"

#include "InventatoryScanCore.h"
#include "InventatoryScanErrorCodes.h"
#include "InventatoryScanUiPolicy.h"
#include "config/config.h"
#include "config/hardware.h"
#include "config/version.h"
#include "log/device_log.h"
#include "ota/firmware_validity.h"

#include <WiFi.h>

#include <cstddef>

namespace inventatory_scan {

namespace {

// Pins the board already drives or reads. The backlight and charger pins must not reuse one.
constexpr int kClaimedPins[] = {
    KEYPAD_COLUMN_1_PIN, KEYPAD_COLUMN_2_PIN, KEYPAD_COLUMN_3_PIN, KEYPAD_COLUMN_4_PIN,
    KEYPAD_ROW_1_PIN,    KEYPAD_ROW_2_PIN,    KEYPAD_ROW_3_PIN,    KEYPAD_ROW_4_PIN,
    BATTERY_ADC_PIN,     GM65_TX_PIN,         GM65_RX_PIN,
    hardware::kLcdClockPin, hardware::kLcdDcPin, hardware::kLcdResetPin, hardware::kLcdCsPin,
    hardware::kLcdDataPin,
};
constexpr std::size_t kClaimedPinCount = sizeof(kClaimedPins) / sizeof(kClaimedPins[0]);

// Straps, RGB and SCK LEDs, USB, in-package flash and PSRAM (AGENTS.md section 3).
constexpr bool pinIsReserved(int pin) {
  return pin == 0 || pin == 3 || pin == 45 || pin == 46 || pin == 48 || pin == 19 || pin == 20 ||
         (pin >= 26 && pin <= 37);
}

constexpr bool pinIsClaimed(int pin, const int* pins, std::size_t count) {
  return count == 0 ? false : (pins[count - 1] == pin || pinIsClaimed(pin, pins, count - 1));
}

static_assert(!pinIsReserved(hardware::kBacklightPin), "kBacklightPin is a strap, LED, USB or flash pin");
static_assert(!pinIsReserved(hardware::kChargingLedPin), "kChargingLedPin is a strap, LED, USB or flash pin");
static_assert(!pinIsClaimed(hardware::kBacklightPin, kClaimedPins, kClaimedPinCount),
              "kBacklightPin is already used by the keypad, scanner, battery or LCD");
static_assert(!pinIsClaimed(hardware::kChargingLedPin, kClaimedPins, kClaimedPinCount),
              "kChargingLedPin is already used by the keypad, scanner, battery or LCD");
static_assert(hardware::kBacklightPin != hardware::kChargingLedPin, "backlight and charging LED share a pin");
static_assert(!pinIsReserved(hardware::kConnectorSensePin), "kConnectorSensePin is a strap, LED, USB or flash pin");
static_assert(!pinIsClaimed(hardware::kConnectorSensePin, kClaimedPins, kClaimedPinCount),
              "kConnectorSensePin is already used by the keypad, scanner, battery or LCD");
static_assert(hardware::kConnectorSensePin != hardware::kBacklightPin, "charger input and backlight share a pin");
static_assert(hardware::kConnectorSensePin != hardware::kChargingLedPin, "charger input and charging LED share a pin");

// NVS namespaces are limited to 15 characters plus the terminator.
constexpr char kOutboxNamespace[] = "inv_outbox";
static_assert(sizeof(kOutboxNamespace) <= 16, "NVS namespace exceeds ESP32's 15-character limit");

// Boot splash hold; the mark blooms in over its first part.
constexpr unsigned long kBootSplashHoldMs = 1200UL;

// Per-step timeout when starting the network worker.
constexpr unsigned long kNetworkStepTimeoutMs = 1000UL;

// Waits for done() for up to kNetworkStepTimeoutMs; true if it became true in time.
template <typename Done>
bool waitFor(Done done) {
  const unsigned long deadline = millis() + kNetworkStepTimeoutMs;
  while (!done() && static_cast<long>(deadline - millis()) > 0) delay(1);
  return done();
}

}  // namespace

bool InventatoryScanApp::begin() {
  deviceLog.println("Inventatory Scan R1 booting");
  // Before any WiFi.mode(): the driver picks flash or RAM storage once, at its first start, and in flash
  // it keeps its own copy of the SSID, password and PMK. A copy left by an earlier build is erased too.
  WiFi.persistent(false);
  if (!SetupStateStore::clearWifiDriverCopy()) deviceLog.println("Could not erase the Wi-Fi driver's stored settings.");
  display_.begin();
  // Lit for the splash; the configured mode takes over in loop().
  backlight_.begin();
  backlight_.setLit(true);
  lastUserActivityAt_ = millis();
  battery_.begin();
  display_.playBootSplash(kBootSplashHoldMs);
  keypadInit();
  if (!deviceSettings_.begin()) {
    deviceLog.println("Device settings storage unavailable; using default contrast.");
  } else {
    lcdContrast_ = deviceSettings_.contrast();
    display_.setContrast(lcdContrast_);
    backlightMode_ = backlightModeFromStored(deviceSettings_.backlightMode());
    energySavingOn_ = deviceSettings_.energySavingEnabled();
    dockMode_ = deviceSettings_.dockModeEnabled();
    sleepDelay_ = sleepDelayFromStored(deviceSettings_.sleepDelay());
    powerOffDelay_ = powerOffDelayFromStored(deviceSettings_.powerOffDelay());
    batteryCapacity_ = batteryCapacityFromStored(deviceSettings_.batteryCapacity());
  }
  // Gauge resumes from the state saved at power-off; a first boot uses the voltage. The capacity is set first,
  // because the stored charge is a share of it.
  BatteryGaugeState storedBattery;
  batteryTracker_.setCapacity(batteryCapacityUah(batteryCapacity_));
  // Read before Wi-Fi starts: after a long time off this is the cell's resting voltage, the gauge's best reading.
  const uint32_t offMs = takeBatteryOffMs();
  if (batteryStore_.begin() && batteryStore_.load(storedBattery)) batteryTracker_.restore(storedBattery, offMs);
  const uint32_t bootMillivolts = battery_.readMillivolts();
  if (offMs == kBatteryOffUnknownMs) {
    deviceLog.printf("Battery %lu mV, time off unknown\r\n", static_cast<unsigned long>(bootMillivolts));
  } else {
    deviceLog.printf("Battery %lu mV, off for %lu min\r\n", static_cast<unsigned long>(bootMillivolts),
                     static_cast<unsigned long>(offMs / 60000UL));
  }
  // Charger signals are sampled once here. A level at boot is not an edge, so nothing starts.
  chargerLedLit_ = signalHighNext(false, battery_.chargingLedMillivolts());
  chargerPlugged_ = signalHighNext(false, battery_.connectorMillivolts());
  lastChargerPlugged_ = chargerPlugged_;
  lastChargerSampleAt_ = millis();
  batteryTracker_.update(bootMillivolts, chargerLedLit_, millis(), batteryLoadMilliamps(), chargerPlugged_);
  batterySampled_ = true;
  lastBatterySampleAt_ = millis();
  lastCharging_ = batteryTracker_.docked();
  lastChargeDone_ = batteryTracker_.chargeDone();
  display_.setBatteryStatus(batteryTracker_.percent(), lastCharging_, lastChargeDone_, batteryEstimateText());
  // A low cell that is not charging is refused: show why, then sleep again. The gauge decides, and the voltage
  // counts only at the floor. A charger that is plugged in but not charging does not count as charging.
  const int bootPercent = batteryTracker_.refusePercent();
  deviceLog.printf("Boot battery: estimate %d%%, refusal at %d%%, charger LED %s.\r\n", batteryTracker_.estimatePercent(),
                   bootPercent, chargerLedLit_ ? "lit" : "dark");
  if (bootPercent >= 0 && bootPercent <= kBatteryLowPercent && !chargerLedLit_) {
    powerOffWithNotice(bootPercent <= kBatteryCriticalPercent ? String("Battery empty") : String("Battery low"),
                       String("Charge to power on"));
  }
  if (!scanner_.begin(9600)) {
    deviceLog.println("Scanner initialization failed; verify the GM65 connection and profile.");
    display_.showBoot("Scanner error", kScannerFaultCode);
    display_.tick();
    delay(2000);
  }
  scanner_.flushInput();
  display_.showBoot({});
  display_.tick();
  powerMode_ = PowerMode::Normal;
  otaRequested_ = false;
  otaActive_ = false;
  lastSyncAttempt_ = 0;
  // Drawn at the first label print, once Wi-Fi runs: before the radio starts esp_random() is only
  // pseudo-random, and a nonce repeated across boots could match a print result the desktop cached.
  bootNonce_ = 0;
  syncSequence_ = 1;
  lookupSequence_ = 1;

  clientConfig_.deviceId = DEVICE_ID;
  clientConfig_.fallbackHost = FALLBACK_HOST;
  clientConfig_.fallbackPort = FALLBACK_PORT;

  // A new or erased outbox starts at a random sequence point, so event ids cannot repeat.
  if (!outbox_.begin(kOutboxNamespace, DEVICE_ID, freshOutboxSequenceStart(esp_random()))) {
    deviceLog.println("Outbox storage unavailable; scans cannot be queued until NVS recovers.");
  } else {
    if (!outbox_.load()) {
      deviceLog.println("Outbox storage was reset; queued events could not be restored.");
    }
    deviceLog.printf("Outbox ready: %u queued, %u free NVS entries.\r\n", static_cast<unsigned>(outbox_.size()),
                  static_cast<unsigned>(outbox_.freeStorageEntries()));
  }
  if (!setupStore_.begin() || !setupStore_.load(provisionedSettings_)) {
    setupMode_ = true;
    if (!provisioning_.begin(clientConfig_.deviceId)) {
      deviceLog.println("BLE provisioning could not start");
      display_.showBoot("Bluetooth error", kBluetoothStartFailedCode);
      display_.tick();
      return false;
    }
    display_.showSetup(provisioning_.pairingCode());
    display_.tick();
    return true;
  }
  if (beginNormalRuntime(provisionedSettings_)) return true;
  deviceLog.println("Normal runtime initialization failed; returning to BLE setup");
  return enterSetupAfterRuntimeFailure();
}

void InventatoryScanApp::loop() {
  loopStarted_ = true;
  deviceLog.serviceOverAir(millis());
  serviceChargerInput(millis());
  serviceBattery();
  serviceEnergySaving();
  serviceBacklight();
  // Fault mark on every screen, setup included, while the scanner is not running.
  display_.setScannerFault(scannerFaulted());
  if (setupMode_) {
    InventatoryKeyEvent event;
    while (keypadPoll(event)) handleKey(event);
    serviceProvisioning();
    display_.tick();
    delay(10);
    return;
  }
  updateConnectivityDisplay();
  display_.tick();
  InventatoryKeyEvent event;
  while (keypadPoll(event)) {
    handleKey(event);
    if (otaRequested_) {
      break;
    }
  }

  if (powerMode_ == PowerMode::Standby) {
    if (serviceOta()) {
      display_.tick();
      delay(2);
      return;
    }
    display_.tick();
    delay(20);
    return;
  }

  // Drain UART before Wi-Fi work, which may block briefly.
  pollScanner();

  if (serviceOta()) {
    display_.tick();
    delay(2);
    return;
  }

  processNetworkResult();
  maybeSync();
  // Drain bytes received during the HTTP call.
  pollScanner();
  updateConnectivityDisplay();
  display_.tick();
  delay(2);
}

bool InventatoryScanApp::beginNormalRuntime(const ProvisionedSettings& settings) {
  provisionedSettings_ = settings;
  otaRequested_ = false;
  otaActive_ = false;
  clientConfig_.token = settings.deviceToken;
  clientConfig_.transportCounterStore = &setupStore_;

  if (networkTask_.load() == nullptr) {
    networkInitializationComplete_ = false;
    networkInitializationSucceeded_ = false;
    networkStarted_ = false;
    wifiStarted_ = false;
    networkPauseAcknowledged_ = false;
    networkPowerOffRequested_ = false;
    networkPaused_ = false;
    if (!startNetworkWorker()) return false;
  }

  const auto taskGone = [this]() { return networkTask_.load() == nullptr; };
  waitFor([&]() { return taskGone() || networkInitializationComplete_.load(); });
  if (!networkInitializationComplete_ || !networkInitializationSucceeded_ || taskGone()) return false;

  waitFor([&]() { return taskGone() || networkStarted_.load(); });
  if (!networkStarted_ || taskGone()) return false;

  if (networkPaused_) {
    waitFor([&]() { return taskGone() || networkPauseAcknowledged_.load(); });
    if (!taskGone() && !networkPauseAcknowledged_) return false;

    networkPowerOffRequested_ = false;
    networkPaused_ = false;
    waitFor([&]() { return taskGone() || !networkPauseAcknowledged_.load(); });
    if (taskGone() || networkPauseAcknowledged_) return false;
  }

  sendDebugMessage("boot");
  logState("boot");
  updateConnectivityDisplay();
  display_.showReady();
  display_.tick();
  return true;
}

bool InventatoryScanApp::enterSetupAfterRuntimeFailure() {
  setupMode_ = true;
  if (!pauseNetworkForSetupTransition()) {
    setupRestartPending_ = true;
    display_.showBoot("Network busy");
    display_.tick();
    return true;
  }

  setupRestartPending_ = false;
  if (!provisioning_.begin(clientConfig_.deviceId)) {
    deviceLog.println("BLE provisioning could not restart after runtime failure");
    display_.showBoot("Bluetooth error", kBluetoothStartFailedCode);
    display_.tick();
    return false;
  }
  display_.showSetup(provisioning_.pairingCode(), "NETWORK ERROR");
  display_.tick();
  return true;
}

void InventatoryScanApp::serviceProvisioning() {
  if (setupRestartPending_) {
    enterSetupAfterRuntimeFailure();
    return;
  }

  provisioning_.service();
  if (provisioning_.takePasskeyChanged() && !setupWifiConnecting_) {
    display_.showSetup(provisioning_.pairingCode(),
                       provisioning_.locked() ? "PAIRING LOCKED" : "BLE READY");
  }

  ProvisionedSettings request;
  if (provisioning_.takeRequest(request)) {
    provisionedSettings_ = request;
    setupWifiConnecting_ = true;
    setupWifiStartedAt_ = millis();
    WiFi.mode(WIFI_STA);
    WiFi.begin(request.wifiSsid.c_str(), request.wifiPassword.c_str());
    display_.showSetup(provisioning_.pairingCode(), "CONNECTING WIFI");
  }
  if (!setupWifiConnecting_) return;
  if (WiFi.status() == WL_CONNECTED) {
    setupWifiConnecting_ = false;
    // Keep the record incomplete until the runtime starts, so a reset during BLE teardown
    // returns to setup rather than booting into an unreachable runtime.
    if (!setupStore_.savePending(provisionedSettings_)) {
      provisioning_.setResult(false, "SAVE FAILED");
      display_.showSetup(provisioning_.pairingCode(), "SAVE FAILED");
      WiFi.disconnect(false);
      return;
    }
    deviceLog.println("Provisioning: credentials staged");
    provisioning_.setResult(true, "WIFI OK");
    // Let the desktop client receive the terminal notification before BLE is torn down.
    delay(300);
    provisioning_.end();
    setupMode_ = false;
    scanner_.flushInput();
    if (!beginNormalRuntime(provisionedSettings_) || !setupStore_.commit()) {
      // Bluetooth was released and cannot restart this boot. The setup record is still incomplete,
      // so the restart opens pairing again.
      deviceLog.println("Provisioning: normal runtime did not become ready; restarting into BLE setup");
      display_.showBoot("Network error", kNetworkStartFailedCode);
      display_.tick();
      delay(2000);
      ESP.restart();
    } else {
      deviceLog.println("Provisioning: normal runtime ready");
    }
    return;
  }
  if (millis() - setupWifiStartedAt_ > 20000UL) {
    setupWifiConnecting_ = false;
    WiFi.disconnect(false);
    provisioning_.setResult(false, "WIFI FAILED");
    display_.showSetup(provisioning_.pairingCode(), "WIFI FAILED");
  }
}

}  // namespace inventatory_scan
