// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Inventatory Scan R1 application controller implementation.

#include "app/inventatory_scan_app.h"

#include "InventatoryScanCore.h"
#include "InventatoryScanErrorCodes.h"
#include "InventatoryScanUiPolicy.h"
#include "config/config.h"
#include "config/hardware.h"
#include "config/version.h"

#include <WiFi.h>

#include <cstddef>

namespace inventatory_scan {

namespace {

// Pins that the board already drives or reads, from config.h and hardware.h. The backlight and
// charger LEDs must not share one with them, nor with each other.
constexpr int kClaimedPins[] = {
    KEYPAD_COLUMN_1_PIN, KEYPAD_COLUMN_2_PIN, KEYPAD_COLUMN_3_PIN, KEYPAD_COLUMN_4_PIN,
    KEYPAD_ROW_1_PIN,    KEYPAD_ROW_2_PIN,    KEYPAD_ROW_3_PIN,    KEYPAD_ROW_4_PIN,
    BATTERY_ADC_PIN,     GM65_TX_PIN,         GM65_RX_PIN,
    hardware::kLcdClockPin, hardware::kLcdDcPin, hardware::kLcdResetPin, hardware::kLcdCsPin,
    hardware::kLcdDataPin,
};
constexpr std::size_t kClaimedPinCount = sizeof(kClaimedPins) / sizeof(kClaimedPins[0]);

// Strapping pins, the RGB and SCK LEDs, USB, and the in-package flash and PSRAM (see AGENTS.md section 3).
constexpr bool pinIsReserved(int pin) {
  return pin == 0 || pin == 3 || pin == 45 || pin == 46 || pin == 48 || pin == 19 || pin == 20 ||
         (pin >= 26 && pin <= 37);
}

constexpr bool pinIsClaimed(int pin, const int* pins, std::size_t count) {
  return count == 0 ? false : (pins[count - 1] == pin || pinIsClaimed(pin, pins, count - 1));
}

static_assert(!pinIsReserved(hardware::kBacklightPin), "kBacklightPin is a strap, LED, USB or flash pin");
static_assert(!pinIsReserved(hardware::kChargingLedPin), "kChargingLedPin is a strap, LED, USB or flash pin");
static_assert(!pinIsReserved(hardware::kChargeDoneLedPin), "kChargeDoneLedPin is a strap, LED, USB or flash pin");
static_assert(!pinIsClaimed(hardware::kBacklightPin, kClaimedPins, kClaimedPinCount),
              "kBacklightPin is already used by the keypad, scanner, battery or LCD");
static_assert(!pinIsClaimed(hardware::kChargingLedPin, kClaimedPins, kClaimedPinCount),
              "kChargingLedPin is already used by the keypad, scanner, battery or LCD");
static_assert(!pinIsClaimed(hardware::kChargeDoneLedPin, kClaimedPins, kClaimedPinCount),
              "kChargeDoneLedPin is already used by the keypad, scanner, battery or LCD");
static_assert(hardware::kBacklightPin != hardware::kChargingLedPin, "backlight and charging LED share a pin");
static_assert(hardware::kBacklightPin != hardware::kChargeDoneLedPin, "backlight and charging LED share a pin");
static_assert(hardware::kChargingLedPin != hardware::kChargeDoneLedPin, "the two charging LEDs share a pin");

// ESP32 NVS namespaces are limited to 15 characters plus the terminator.
constexpr char kOutboxNamespace[] = "inv_outbox";
static_assert(sizeof(kOutboxNamespace) <= 16, "NVS namespace exceeds ESP32's 15-character limit");

// The boot splash stays up this long, the mark blooming in over its first part, before the normal
// screens take over.
constexpr unsigned long kBootSplashHoldMs = 1200UL;

// How long each step of starting the network worker may take.
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
  Serial.println("Inventatory Scan R1 booting");
  display_.begin();
  // The backlight is lit for the splash; the configured mode takes over in the loop.
  backlight_.begin();
  backlight_.setLit(true);
  lastUserActivityAt_ = millis();
  battery_.begin();
  display_.playBootSplash(kBootSplashHoldMs);
  keypadInit();
  // The gauge resumes from the state saved at the last power-off. A first boot takes the voltage.
  BatteryGaugeState storedBattery;
  if (batteryStore_.begin() && batteryStore_.load(storedBattery)) batteryTracker_.restore(storedBattery);
  const uint32_t bootMillivolts = battery_.readMillivolts();
  Serial.printf("Battery %lu mV\r\n", static_cast<unsigned long>(bootMillivolts));
  const ChargeState bootCharge = battery_.chargeState();
  batteryTracker_.update(bootMillivolts, bootCharge != ChargeState::Unplugged, millis(),
                         bootCharge == ChargeState::Done, batteryLoadMilliamps());
  batterySampled_ = true;
  lastBatterySampleAt_ = millis();
  lastCharging_ = batteryTracker_.charging();
  lastChargeDone_ = batteryTracker_.chargeDone();
  display_.setBatteryStatus(batteryTracker_.percent(), lastCharging_, lastChargeDone_, batteryEstimateText());
  // A cell that is low and not docked is refused: show why, then sleep again. Low is judged by the
  // lower of the estimate and the voltage.
  const int bootPercent = batteryTracker_.refusePercent();
  if (bootPercent >= 0 && bootPercent <= kBatteryLowPercent && !lastCharging_) {
    powerOffWithNotice(bootPercent <= kBatteryCriticalPercent ? String("Battery empty") : String("Battery low"),
                       String("Charge to power on"));
  }
  if (!scanner_.begin(9600)) {
    Serial.println("Scanner initialization failed; verify the GM65 connection and profile.");
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
  bootNonce_ = esp_random() & 0xFFFFFFUL;
  syncSequence_ = 1;
  lookupSequence_ = 1;

  clientConfig_.deviceId = DEVICE_ID;
  clientConfig_.fallbackHost = FALLBACK_HOST;
  clientConfig_.fallbackPort = FALLBACK_PORT;

  // A new or erased outbox starts its event sequence at a random point, so event ids
  // cannot repeat ids the desktop already holds for this device.
  if (!outbox_.begin(kOutboxNamespace, DEVICE_ID, freshOutboxSequenceStart(esp_random()))) {
    Serial.println("Outbox storage unavailable; scans cannot be queued until NVS recovers.");
  } else {
    if (!outbox_.load()) {
      Serial.println("Outbox storage was reset; queued events could not be restored.");
    }
    Serial.printf("Outbox ready: %u queued, %u free NVS entries.\r\n", static_cast<unsigned>(outbox_.size()),
                  static_cast<unsigned>(outbox_.freeStorageEntries()));
  }
  if (!deviceSettings_.begin()) {
    Serial.println("Device settings storage unavailable; using default contrast.");
  } else {
    lcdContrast_ = deviceSettings_.contrast();
    display_.setContrast(lcdContrast_);
    backlightMode_ = backlightModeFromStored(deviceSettings_.backlightMode());
    energySavingOn_ = deviceSettings_.energySavingEnabled();
    sleepDelay_ = sleepDelayFromStored(deviceSettings_.sleepDelay());
    powerOffDelay_ = powerOffDelayFromStored(deviceSettings_.powerOffDelay());
  }
  if (!setupStore_.begin() || !setupStore_.load(provisionedSettings_)) {
    setupMode_ = true;
    if (!provisioning_.begin(clientConfig_.deviceId)) {
      Serial.println("BLE provisioning could not start");
      display_.showBoot("Bluetooth error", kBluetoothStartFailedCode);
      display_.tick();
      return false;
    }
    display_.showSetup(provisioning_.pairingCode());
    display_.tick();
    return true;
  }
  if (beginNormalRuntime(provisionedSettings_)) return true;
  Serial.println("Normal runtime initialization failed; returning to BLE setup");
  return enterSetupAfterRuntimeFailure();
}

void InventatoryScanApp::loop() {
  serviceBattery();
  serviceEnergySaving();
  serviceBacklight();
  // Every screen, including setup, shows the fault mark while the scanner is not running.
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

  // Drain UART before any Wi-Fi operation that may briefly block.
  pollScanner();

  if (serviceOta()) {
    display_.tick();
    delay(2);
    return;
  }

  processNetworkResult();
  maybeSync();
  // Drain anything received while the HTTP client was active.
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
    Serial.println("BLE provisioning could not restart after runtime failure");
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
    // Keep the record incomplete until the normal runtime has actually been
    // started. A reset while BLE is being torn down must return to setup mode,
    // not boot into a credentialed but unreachable runtime.
    if (!setupStore_.savePending(provisionedSettings_)) {
      provisioning_.setResult(false, "SAVE FAILED");
      display_.showSetup(provisioning_.pairingCode(), "SAVE FAILED");
      WiFi.disconnect(false);
      return;
    }
    Serial.println("Provisioning: credentials staged");
    provisioning_.setResult(true, "WIFI OK");
    // Give the subscribed desktop client a scheduling window to receive the
    // terminal notification before BLE is deinitialized below.
    delay(300);
    provisioning_.end();
    setupMode_ = false;
    scanner_.flushInput();
    if (!beginNormalRuntime(provisionedSettings_) || !setupStore_.commit()) {
      // Bluetooth was released above and cannot start again in this boot. The setup record is still
      // incomplete, so the restarted device opens pairing again.
      Serial.println("Provisioning: normal runtime did not become ready; restarting into BLE setup");
      display_.showBoot("Network error", kNetworkStartFailedCode);
      display_.tick();
      delay(2000);
      ESP.restart();
    } else {
      Serial.println("Provisioning: normal runtime ready");
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
