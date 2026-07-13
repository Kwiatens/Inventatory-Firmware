#pragma once

#include "HimsScanCore.h"
#include "HimsScanSync.h"
#include "display/display_controller.h"
#include "gm65/gm65_scanner.h"
#include "keypad/keypad.h"
#include "net/hims_client.h"
#include "HimsScanOutbox.h"
#include "storage/outbox_store.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace hims_scan {

class HimsScanApp {
 public:
  bool begin();
  void loop();

 private:
  enum class State {
    Idle,
    AwaitQuantity,
  };

  enum class PowerMode {
    Normal,
    Standby,
  };

  enum class PendingScanType {
    None,
    HimsQuantity,
    DigiKeyDataMatrix,
  };

  void handleScan(const String& code);
  void pollScanner();
  bool queueEvent(DeviceEventKind kind, const String& code, int value);
  void handleKey(const HimsKeyEvent& event);
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

  Gm65Scanner scanner_;
  OutboxStore outbox_;
  DisplayController display_;
  HimsClient client_;
  HimsClientConfig clientConfig_;
  QuantityComposer quantity_;
  String scannedCode_;
  State state_ = State::Idle;
  PendingScanType pendingScanType_ = PendingScanType::None;
  PowerMode powerMode_ = PowerMode::Normal;
  bool wifiStarted_ = false;
  bool wifiConnectedReported_ = false;
  bool himsSoftwarePrimed_ = false;
  bool otaRequested_ = false;
  bool otaActive_ = false;
  bool pcOnline_ = false;
  bool syncInFlight_ = false;
  bool networkStarted_ = false;
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
  String activeLookupId_;
  String activeLookupCode_;
  unsigned long lastReconnectAttempt_ = 0;
  unsigned long lastSyncAttempt_ = 0;
  uint32_t syncSequence_ = 1;
  uint32_t lookupSequence_ = 1;
  uint8_t lookupMissCount_ = 0;
};

}  // namespace hims_scan
