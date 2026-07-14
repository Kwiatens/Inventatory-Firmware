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
    PrintingLabel,
  };

  enum class PowerMode {
    Normal,
    Standby,
  };

  enum class PendingScanType {
    None,
    InventatoryQuantity,
    DigiKeyDataMatrix,
  };

  void handleScan(const String& code);
  void pollScanner();
  bool queueEvent(DeviceEventKind kind, const String& code, int value, String* eventId = nullptr);
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
  State state_ = State::Idle;
  PendingScanType pendingScanType_ = PendingScanType::None;
  PowerMode powerMode_ = PowerMode::Normal;
  bool wifiStarted_ = false;
  bool wifiConnectedReported_ = false;
  bool inventatorySoftwarePrimed_ = false;
  bool otaRequested_ = false;
  bool otaActive_ = false;
  bool pcOnline_ = false;
  bool syncInFlight_ = false;
  bool networkStarted_ = false;
  bool setupMode_ = false;
  bool setupWifiConnecting_ = false;
  unsigned long setupWifiStartedAt_ = 0;
  volatile bool networkPaused_ = false;
  volatile bool networkPauseAcknowledged_ = false;
  volatile bool networkPowerOffRequested_ = false;
  volatile bool networkBusy_ = false;
  TaskHandle_t networkTask_ = nullptr;
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
  unsigned long lastReconnectAttempt_ = 0;
  unsigned long lastSyncAttempt_ = 0;
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
};

}  // namespace inventatory_scan
