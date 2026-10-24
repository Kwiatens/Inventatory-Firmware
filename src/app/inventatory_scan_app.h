// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "InventatoryScanBattery.h"
#include "InventatoryScanBlockedHead.h"
#include "InventatoryScanCore.h"
#include "InventatoryScanFirmwarePackage.h"
#include "InventatoryScanSync.h"
#include "InventatoryScanUiPolicy.h"
#include "display/backlight.h"
#include "display/display_controller.h"
#include "gm65/gm65_scanner.h"
#include "keypad/keypad.h"
#include "net/inventatory_client.h"
#include "power/battery_monitor.h"
#include "InventatoryScanOutbox.h"
#include "storage/outbox_store.h"
#include "storage/battery_state_store.h"
#include "storage/device_settings_store.h"
#include "setup/ble_provisioning.h"
#include "setup/setup_state_store.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>

namespace inventatory_scan {

class InventatoryScanApp {
 public:
  bool begin();
  void loop();

 private:
  enum class State {
    Idle,
    AwaitQuantity,
    WaitingForResult,
    Menu,
    FaultDetail,
    QuickLabels,
    QuickSettings,
    DisplaySettings,
    AdjustContrast,
    AdjustBacklight,
    EnergySaving,
    AdjustSleepAfter,
    AdjustPowerOff,
    AwaitBatteryCapacity,
    ConfirmReprovision,
    ConfirmDiscard,
    ConfirmRepair,
    PrintingLabel,
  };

  enum class PowerMode {
    Normal,
    Standby,
  };

  enum class PendingScanType {
    None,
    InventatoryQuantity,
    ComponentDataMatrix,
  };

  void handleScan(const String& code);
  void pollScanner();
  void showScanRefusal(const char* screenMessage, const char* logMessage);
  bool queueEvent(DeviceEventKind kind, const String& code, int value, String* eventId = nullptr,
                  const String& manufacturer = String(), const String& manufacturerPartNumber = String(),
                  const String& encodedPartName = String());
  void waitForInventoryResult(const String& eventId);
  void handleKey(const InventatoryKeyEvent& event);
  void handleDigit(char digit);
  void handleCancel();
  void handleAddSubtract(bool add);
  void confirmReprovision();
  void confirmRepair();
  void confirmDiscard();
  void openMenu();
  void returnToReady();
  // A failed result for a problem on the device itself, shown without waiting for the PC.
  static SyncResult localFailure(const char* id, const char* code, const char* message);
  void requestOtaUpdate();
  // The desktop's sync response says whether it offers an update; the Ready screen shows it.
  void noteFirmwareOffer(bool offered);
  void sendDebugMessage(const String& message, const String& level = "info");
  bool startNetworkWorker();
  static void networkTaskEntry(void* context);
  void networkTaskLoop();
  bool queueSyncJob(const String& requestId, const String& payload);
  void processNetworkResult();
  bool serviceOta();
  void fetchOtaOffer();
  void acceptOtaUpdate();
  void declineOtaUpdate(const char* reason);
  void installOtaUpdate();
  void sendOtaStatus(const char* state, const char* error, const char* reason);
  void failOtaUpdate(FirmwareCheck check);
  void finishOtaQuietly();
  bool ensureWiFiForOta();
  void recoverFromOtaFailure();
  void abandonOtaWait(const char* reason, bool showFailure);
  void enterStandby();
  void exitStandby();
  void enterPowerOff();
  void noteUserActivity();
  void serviceBattery();
  void serviceChargerInput(unsigned long now);
  void onPlugged(unsigned long now);
  void onUnplugged();
  void serviceDockPowerOff(unsigned long now);
  void saveBatteryStateIfDue(unsigned long now);
  void saveBatteryState();
  uint32_t batteryLoadMilliamps() const;
  void serviceBacklight();
  void serviceEnergySaving();
  void startEnergySleepWarning();
  void enterEnergySleep();
  void startEnergyOffWarning();
  void updateEnergyWarning(unsigned long now);
  void dismissEnergyWarning();
  void powerOffForEnergySaving();
  bool energyWarningShown() const {
    return energyPhase_ == EnergyPhase::SleepWarning || energyPhase_ == EnergyPhase::OffWarning;
  }
  void startLowBatteryWarning();
  void updateLowBatteryWarning(unsigned long now);
  void cancelLowBatteryWarning();
  void powerOffForBattery();
  void powerOffWithNotice(const String& title, const String& detail);
  void startPowerOffWarning();
  void cancelPowerOffWarning();
  String batteryEstimateText() const;
  bool canShowBatteryCard() const;
  void submitCurrent(char action);
  void maybeSync();
  void handleSyncResponse(const SyncResponse& response);
  void updateConnectivityDisplay();
  bool scannerFaulted() const;
  DisplayConnectivityState connectivityState() const;
  String syncMode() const;
  void reconnectWiFiWorker();
  void logState(const char* reason) const;
  void setAwaitingQuantity();
  void resetPending();
  void openQuickLabels();
  void openQuickSettings();
  void openDisplaySettings();
  void openBacklightSettings();
  void stepBacklightMode(int step);
  void persistBacklightMode();
  void openEnergySaving();
  void toggleEnergySaving();
  void persistEnergySaving();
  void toggleDockMode();
  void persistDockMode();
  void openSleepAfterSettings();
  void stepSleepDelay(int step);
  void persistSleepDelay();
  void openPowerOffSettings();
  void stepPowerOffDelay(int step);
  void persistPowerOffDelay();
  void cycleEnergyPage();
  void openBatteryCapacityEntry();
  void refreshBatteryCapacityEntry(bool rejected);
  void appendBatteryCapacityDigit(char digit);
  void deleteBatteryCapacityDigit();
  void confirmBatteryCapacity();
  void persistBatteryCapacity();
  void requestQuickLabelPrint(uint8_t presetIndex);
  void refreshQuickLabels();
  void persistContrast();
  void serviceProvisioning();
  bool beginNormalRuntime(const ProvisionedSettings& settings);
  bool enterSetupAfterRuntimeFailure();
  bool pauseNetworkForSetupTransition();
  void resumeNetworkAfterSetupTransition();

  Gm65Scanner scanner_;
  OutboxStore outbox_;
  DeviceSettingsStore deviceSettings_;
  SetupStateStore setupStore_;
  BleProvisioning provisioning_;
  ProvisionedSettings provisionedSettings_;
  DisplayController display_;
  BatteryMonitor battery_;
  BatteryTracker batteryTracker_;
  BatteryStateStore batteryStore_;
  Backlight backlight_;
  BacklightMode backlightMode_ = BacklightMode::On;
  // Off by default. The delays apply only while on.
  bool energySavingOn_ = false;
  // Dock mode: standby shortly after the charger is plugged in, and back on when it is removed.
  // It has its own switch, so it works without energy saving.
  bool dockMode_ = false;
  // Dock-mode standby that is scheduled, and when it is due.
  bool dockPowerOffPending_ = false;
  unsigned long dockPowerOffAt_ = 0;
  SleepDelay sleepDelay_ = SleepDelay::Min5;
  PowerOffDelay powerOffDelay_ = PowerOffDelay::Min30;
  // Energy list page (0 or 1), the battery capacity the estimate counts in (mAh), and the capacity being typed.
  uint8_t energyPage_ = 0;
  uint16_t batteryCapacity_ = kBatteryDefaultCapacityMah;
  uint16_t batteryEntryMah_ = 0;
  EnergyPhase energyPhase_ = EnergyPhase::Awake;
  // Sleep start; the power-off countdown runs from here.
  unsigned long sleepStartedAt_ = 0;
  // Seconds last shown on a warning; redraw only when the count changes.
  int energyShownSeconds_ = -1;
  bool batterySampled_ = false;
  unsigned long lastBatterySampleAt_ = 0;
  // Last NVS save of the gauge state, and the percent saved.
  unsigned long lastBatterySaveAt_ = 0;
  int batterySavedPercent_ = -1;
  // On the charger at the last sample; a change shows the dock card.
  bool lastCharging_ = false;
  bool lastChargeDone_ = false;
  // Charger LED signal (charging), with hysteresis.
  bool chargerLedLit_ = false;
  // Charger plugged in, with hysteresis. Sampled every kChargerSampleIntervalMs.
  bool chargerPlugged_ = false;
  bool lastChargerPlugged_ = false;
  unsigned long lastChargerSampleAt_ = 0;
  // Standby from dock mode. Removing the charger ends it.
  bool dockStandby_ = false;
  // Last serial log line of battery and charger status.
  unsigned long lastBatteryLogAt_ = 0;
  // Latched; clears only when the dock connects.
  bool lowBatteryWarning_ = false;
  unsigned long lowBatteryWarningAt_ = 0;
  int lowBatteryShownSeconds_ = -1;
  // Soft power-off countdown, from the '*' warning to commit or release.
  bool powerOffWarning_ = false;
  // Any key or scan keeps the backlight lit in the Auto modes.
  unsigned long lastUserActivityAt_ = 0;
  InventatoryClient client_;
  InventatoryClientConfig clientConfig_;
  QuantityComposer quantity_;
  String scannedCode_;
  // Vendor bag main code, sent as the event code. Empty when not a vendor bag.
  String scannedEventCode_;
  String scannedManufacturer_;
  String scannedManufacturerPartNumber_;
  String scannedPartName_;
  String deferredScan_;
  State state_ = State::Idle;
  // The screen the fault details were opened from, so C returns to it.
  State faultDetailReturn_ = State::Idle;
  PendingScanType pendingScanType_ = PendingScanType::None;
  std::atomic<PowerMode> powerMode_{PowerMode::Normal};
  std::atomic_bool wifiStarted_{false};
  std::atomic_bool wifiConnectedReported_{false};
  // Re-pair confirmation opened by holding #; C returns to Ready.
  bool reprovisionFromHold_ = false;
  // An update runs Connecting (Wi-Fi and the desktop's offer), AwaitConsent (A installs, C declines),
  // then Installing. otaRequested_ is true for all three; otaActive_ only while flashing.
  enum class OtaPhase : uint8_t { Idle, Connecting, AwaitConsent, Installing };
  OtaPhase otaPhase_ = OtaPhase::Idle;
  bool otaRequested_ = false;
  bool otaActive_ = false;
  // Set once an offer was declined, refused or empty, so the Ready screen stops showing the desktop's
  // offer until a restart. A new request clears it.
  bool otaHintsMuted_ = false;
  // The wait for an offer ends on timeout or C; once the install starts it runs to the end.
  unsigned long otaRequestedAt_ = 0;
  unsigned long otaLastOfferAttempt_ = 0;
  unsigned long otaConsentShownAt_ = 0;
  bool otaUploadStarted_ = false;
  // The verified header of the offered package; cleared when the update ends.
  FirmwarePackageHeader otaHeader_;
  // Last percent drawn; redraw only when it changes.
  int otaDisplayedPercent_ = -1;
  std::atomic_bool pcOnline_{false};
  bool syncInFlight_ = false;
  std::atomic_bool networkStarted_{false};
  std::atomic_bool networkInitializationComplete_{false};
  std::atomic_bool networkInitializationSucceeded_{false};
  bool setupMode_ = false;
  bool setupRestartPending_ = false;
  bool setupWifiConnecting_ = false;
  unsigned long setupWifiStartedAt_ = 0;
  std::atomic_bool networkPaused_{false};
  std::atomic_bool networkPauseAcknowledged_{false};
  std::atomic_bool networkPowerOffRequested_{false};
  std::atomic<TaskHandle_t> networkTask_{nullptr};
  std::atomic_bool networkTaskStartReleased_{false};
  // Read by Core 1 at any time, so atomic. Never freed while the app runs.
  std::atomic<SemaphoreHandle_t> networkMutex_{nullptr};
  bool syncJobPending_ = false;
  String syncJobRequestId_;
  String syncJobPayload_;
  bool syncResultPending_ = false;
  String syncResultRequestId_;
  bool syncResultSent_ = false;
  int syncResultStatus_ = 0;
  String syncResultBody_;
  bool debugJobPending_ = false;
  String debugJobMessage_;
  String debugJobLevel_;
  String pendingResultAck_;
  String awaitingResultEventId_;
  String activeLookupId_;
  String activeLookupCode_;
  std::atomic_ulong lastReconnectAttempt_{0};
  // Written by Core 0 (reconnect) and Core 1 (sync and result handling).
  std::atomic_ulong lastSyncAttempt_{0};
  // Set on the first loop() pass. A deliberate power off or restart after it confirms a pending OTA image;
  // a refusal during begin(), such as the low-battery notice, proves nothing and leaves it pending.
  bool loopStarted_ = false;
  // Random per boot, so request ids that embed a sequence (quick-label prints) do not repeat.
  uint32_t bootNonce_ = 0;
  // When the current quick-label print was first issued. Transient failures retry the same request
  // id, which the desktop answers from its cache, until this window passes.
  unsigned long pendingQuickLabelPrintStartedAt_ = 0;
  uint32_t syncSequence_ = 1;
  uint32_t lookupSequence_ = 1;
  uint8_t lookupMissCount_ = 0;
  uint8_t lcdContrast_ = 45;
  uint8_t quickLabelPage_ = 0;
  bool quickLabelSnapshotReceived_ = false;
  bool pcSupportsQuickLabels_ = false;
  String quickLabelPresets_[kQuickLabelPresetLimit];
  std::size_t quickLabelPresetCount_ = 0;
  uint32_t quickLabelRevision_ = 0;
  String pendingQuickLabelPrintId_;
  uint8_t pendingQuickLabelPresetIndex_ = 0;
  uint32_t pendingQuickLabelRevision_ = 0;
  String lastSyncHeadEventId_;
  BlockedHeadTracker outboxBlockedHead_;
};

}  // namespace inventatory_scan
