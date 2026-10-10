// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "InventatoryScanCore.h"
#include "InventatoryScanSync.h"
#include "display/display_controller.h"
#include "gm65/gm65_scanner.h"
#include "keypad/keypad.h"
#include "net/inventatory_client.h"
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
    QuickLabels,
    QuickSettings,
    AdjustContrast,
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
  void submitCurrent(char action);
  void maybeSync();
  void handleSyncResponse(const SyncResponse& response);
  void updateConnectivityDisplay();
  DisplayConnectivityState connectivityState() const;
  String syncMode() const;
  void reconnectWiFiWorker();
  void logState(const char* reason) const;
  void setAwaitingQuantity();
  void resetPending();
  void openQuickLabels();
  void openQuickSettings();
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
  InventatoryClient client_;
  InventatoryClientConfig clientConfig_;
  QuantityComposer quantity_;
  String scannedCode_;
  String scannedManufacturer_;
  String scannedManufacturerPartNumber_;
  String scannedPartName_;
  String deferredScan_;
  State state_ = State::Idle;
  PendingScanType pendingScanType_ = PendingScanType::None;
  std::atomic<PowerMode> powerMode_{PowerMode::Normal};
  std::atomic_bool wifiStarted_{false};
  std::atomic_bool wifiConnectedReported_{false};
  // True when the re-pair confirmation was opened by holding # (C returns to Ready).
  bool reprovisionFromHold_ = false;
  bool otaRequested_ = false;
  bool otaActive_ = false;
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
  SemaphoreHandle_t networkMutex_ = nullptr;
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
  unsigned long lastSyncAttempt_ = 0;
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
  uint8_t outboxBlockedStreak_ = 0;
  bool outboxBlocked_ = false;
};

}  // namespace inventatory_scan
