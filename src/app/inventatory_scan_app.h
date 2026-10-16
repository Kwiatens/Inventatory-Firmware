// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "InventatoryScanBattery.h"
#include "InventatoryScanBlockedHead.h"
#include "InventatoryScanCore.h"
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
  void sendDebugMessage(const String& message, const String& level = "info");
  bool startNetworkWorker();
  static void networkTaskEntry(void* context);
  void networkTaskLoop();
  bool queueSyncJob(const String& requestId, const String& payload);
  void processNetworkResult();
  bool serviceOta();
  bool startOtaService();
  bool ensureWiFiForOta();
  void recoverFromOtaFailure();
  void abandonOtaWait(const char* reason, bool showFailure);
  void enterStandby();
  void exitStandby();
  void enterPowerOff();
  void noteUserActivity();
  void serviceBattery();
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
  void openSleepAfterSettings();
  void stepSleepDelay(int step);
  void persistSleepDelay();
  void openPowerOffSettings();
  void stepPowerOffDelay(int step);
  void persistPowerOffDelay();
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
  Backlight backlight_;
  BacklightMode backlightMode_ = BacklightMode::On;
  // Energy saving: off by default. The delays apply only while it is on.
  bool energySavingOn_ = false;
  SleepDelay sleepDelay_ = SleepDelay::Min5;
  PowerOffDelay powerOffDelay_ = PowerOffDelay::Min30;
  EnergyPhase energyPhase_ = EnergyPhase::Awake;
  // When the device went to sleep. The power-off countdown runs from here.
  unsigned long sleepStartedAt_ = 0;
  // Whole seconds last shown on a warning, so the screen is redrawn only when the count changes.
  int energyShownSeconds_ = -1;
  bool batterySampled_ = false;
  unsigned long lastBatterySampleAt_ = 0;
  bool lastCharging_ = false;
  // The charger's green "charging complete" LED was lit at the last battery sample.
  bool lastChargeDone_ = false;
  // Latched when the low-battery warning appears; it clears only when the dock connects.
  bool lowBatteryWarning_ = false;
  unsigned long lowBatteryWarningAt_ = 0;
  int lowBatteryShownSeconds_ = -1;
  // The soft power-off countdown, from the '*' warning until the commit or a release.
  bool powerOffWarning_ = false;
  // Any key or scan keeps the backlight lit in the Auto modes.
  unsigned long lastUserActivityAt_ = 0;
  InventatoryClient client_;
  InventatoryClientConfig clientConfig_;
  QuantityComposer quantity_;
  String scannedCode_;
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
  // True when the re-pair confirmation was opened by holding # (C returns to Ready).
  bool reprovisionFromHold_ = false;
  bool otaRequested_ = false;
  bool otaActive_ = false;
  // Set by the ArduinoOTA error callback. The listener is stopped from serviceOta(), never from
  // inside the callback, so an update that failed does not leave the listener bound.
  bool otaEndPending_ = false;
  // OTA must not wait forever: giving up (timeout or C) restores normal operation
  // until an uploader actually starts sending.
  unsigned long otaRequestedAt_ = 0;
  bool otaUploadStarted_ = false;
  // Last percentage shown on the update screen; ArduinoOTA reports progress
  // per received chunk, far more often than the percentage changes.
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
  // Read by Core 1 at any time, so it is atomic. Once created it is never freed while the app runs.
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
  // Random per boot so request ids that embed a restarting sequence number
  // (quick-label prints) cannot repeat across reboots.
  uint32_t bootNonce_ = 0;
  // When the current quick-label print request was first issued; transient
  // failures retry the same request id (the desktop answers repeats from its
  // cache) until this window passes.
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
