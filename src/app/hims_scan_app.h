#pragma once

#include "HimsScanCore.h"
#include "HimsScanSync.h"
#include "display/display_controller.h"
#include "gm65/gm65_scanner.h"
#include "keypad/keypad.h"
#include "net/hims_client.h"
#include "HimsScanOutbox.h"
#include "storage/outbox_store.h"

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
  bool serviceOta();
  bool startOtaService();
  bool ensureWiFiForOta();
  String buildWifiDebugText() const;
  void enterStandby();
  void exitStandby();
  void submitCurrent(char action);
  void maybeSync();
  void handleSyncResponse(const SyncResponse& response);
  String syncMode() const;
  void reconnectWiFi();
  void primeHimsSoftwareConnection();
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
  String pendingResultAck_;
  unsigned long lastReconnectAttempt_ = 0;
  unsigned long lastSyncAttempt_ = 0;
  uint32_t syncSequence_ = 1;
};

}  // namespace hims_scan
