// Inventatory Scan R1 application controller implementation.

#include "app/inventatory_scan_app.h"

#include "InventatoryScanCore.h"
#include "InventatoryScanUiPolicy.h"
#include "config/config.h"
#include "config/version.h"

#include <WiFi.h>

namespace inventatory_scan {

void InventatoryScanApp::sendDebugMessage(const String& message, const String& level) {
  if (networkPaused_ || !WiFi.isConnected() || trimCopy(message).length() == 0) {
    return;
  }
  if (networkMutex_ == nullptr || xSemaphoreTake(networkMutex_, pdMS_TO_TICKS(5)) != pdTRUE) return;
  if (!debugJobPending_) {
    debugJobMessage_ = message;
    debugJobLevel_ = level;
    debugJobPending_ = true;
  }
  xSemaphoreGive(networkMutex_);
}

bool InventatoryScanApp::startNetworkWorker() {
  if (networkTask_ != nullptr) return true;
  networkMutex_ = xSemaphoreCreateMutex();
  if (networkMutex_ == nullptr) {
    Serial.println("Network worker: mutex allocation failed");
    return false;
  }
  TaskHandle_t createdTask = nullptr;
  const auto created = xTaskCreatePinnedToCore(networkTaskEntry, "inventatory-net", 8192, this, 1,
                                                &createdTask, 0);
  if (created != pdPASS) {
    Serial.println("Network worker: task allocation failed");
    vSemaphoreDelete(networkMutex_);
    networkMutex_ = nullptr;
    networkTask_ = nullptr;
    return false;
  }
  networkTask_ = createdTask;
  if (networkInitializationComplete_ && !networkInitializationSucceeded_) networkTask_ = nullptr;
  return true;
}

void InventatoryScanApp::networkTaskEntry(void* context) {
  static_cast<InventatoryScanApp*>(context)->networkTaskLoop();
}

void InventatoryScanApp::networkTaskLoop() {
  const bool clientReady = client_.begin(clientConfig_);
  networkInitializationSucceeded_ = clientReady;
  networkInitializationComplete_ = true;
  if (!clientReady) {
    Serial.println("Network worker: client initialization failed");
    networkStarted_ = false;
    networkTask_ = nullptr;
    auto* failedMutex = networkMutex_;
    networkMutex_ = nullptr;
    if (failedMutex != nullptr) vSemaphoreDelete(failedMutex);
    vTaskDelete(nullptr);
    return;
  }
  wifiStarted_ = true;
  networkStarted_ = true;
  for (;;) {
    if (networkPaused_) {
      if (!networkPauseAcknowledged_) {
        client_.end();
        if (networkPowerOffRequested_) {
          WiFi.disconnect(true);
          WiFi.mode(WIFI_OFF);
          wifiStarted_ = false;
          wifiConnectedReported_ = false;
        }
        networkPauseAcknowledged_ = true;
      }
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    if (networkPauseAcknowledged_) {
      networkPauseAcknowledged_ = false;
      client_.begin(clientConfig_);
      wifiStarted_ = true;
      lastReconnectAttempt_ = 0;
      wifiConnectedReported_ = false;
    }

    reconnectWiFiWorker();

    String requestId;
    String payload;
    String debugMessage;
    String debugLevel;
    bool hasSync = false;
    bool hasDebug = false;
    if (xSemaphoreTake(networkMutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
      if (syncJobPending_) {
        requestId = syncJobRequestId_;
        payload = syncJobPayload_;
        syncJobPending_ = false;
        syncJobRequestId_.clear();
        syncJobPayload_.clear();
        hasSync = true;
      } else if (debugJobPending_) {
        debugMessage = debugJobMessage_;
        debugLevel = debugJobLevel_;
        debugJobPending_ = false;
        debugJobMessage_.clear();
        debugJobLevel_.clear();
        hasDebug = true;
      }
      xSemaphoreGive(networkMutex_);
    }

    if (hasSync) {
      int status = 0;
      String body;
      networkBusy_ = true;
      const bool sent = WiFi.isConnected() && client_.postSync(payload, status, body);
      networkBusy_ = false;
      if (xSemaphoreTake(networkMutex_, portMAX_DELAY) == pdTRUE) {
        syncResultRequestId_ = requestId;
        syncResultSent_ = sent;
        syncResultStatus_ = status;
        syncResultBody_ = body;
        syncResultPending_ = true;
        xSemaphoreGive(networkMutex_);
      }
    } else if (hasDebug && WiFi.isConnected()) {
      networkBusy_ = true;
      Serial.print("Scanner diagnostic [");
      Serial.print(debugLevel.c_str());
      Serial.print("]: ");
      Serial.println(debugMessage.c_str());
      networkBusy_ = false;
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

bool InventatoryScanApp::queueSyncJob(const String& requestId, const String& payload) {
  if (!networkStarted_ || networkMutex_ == nullptr || xSemaphoreTake(networkMutex_, pdMS_TO_TICKS(5)) != pdTRUE) {
    return false;
  }
  const bool available = !syncJobPending_ && !syncResultPending_;
  if (available) {
    syncJobRequestId_ = requestId;
    syncJobPayload_ = payload;
    syncJobPending_ = true;
  }
  xSemaphoreGive(networkMutex_);
  return available;
}

void InventatoryScanApp::reconnectWiFiWorker() {
  if (!wifiStarted_) {
    return;
  }

  if (powerMode_ == PowerMode::Standby) {
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiConnectedReported_) {
      wifiConnectedReported_ = true;
      inventatorySoftwarePrimed_ = false;
      lastSyncAttempt_ = 0;
      // Do not disclose provisioning metadata through the serial console.
      Serial.println("Local Wi-Fi connected");
    }
    return;
  }

  wifiConnectedReported_ = false;
  inventatorySoftwarePrimed_ = false;
  pcOnline_ = false;
  if (lastReconnectAttempt_ != 0 && millis() - lastReconnectAttempt_ < WIFI_RECONNECT_INTERVAL_MS) {
    return;
  }
  lastReconnectAttempt_ = millis();

  if (provisionedSettings_.wifiSsid.length() == 0) {
    Serial.println("Wi-Fi setup is incomplete; staying offline.");
    return;
  }

  Serial.println("Reconnecting to saved local Wi-Fi.");
  WiFi.begin(provisionedSettings_.wifiSsid.c_str(), provisionedSettings_.wifiPassword.c_str());
}

String InventatoryScanApp::syncMode() const {
  if (otaRequested_ || otaActive_) return "ota";
  if (powerMode_ == PowerMode::Standby) return "standby";
  if (state_ == State::AwaitQuantity) return "await_quantity";
  if (state_ == State::WaitingForResult) return "waiting_for_result";
  if (state_ == State::PrintingLabel) return "label_print";
  return "ready";
}

void InventatoryScanApp::maybeSync() {
  if (!WiFi.isConnected()) {
    pcOnline_ = false;
    return;
  }
  const unsigned long interval = (!outbox_.empty() || pendingResultAck_.length() > 0 ||
                                  pendingQuickLabelPrintId_.length() > 0 ||
                                  (state_ == State::QuickLabels && !quickLabelSnapshotReceived_))
                                     ? 1000UL
                                     : 5000UL;
  if (syncInFlight_ || (lastSyncAttempt_ != 0 && millis() - lastSyncAttempt_ < interval)) return;

  DeviceEventRecord events[kSyncBatchSize];
  std::size_t eventCount = 0;
  while (eventCount < kSyncBatchSize && outbox_.peek(eventCount, events[eventCount])) ++eventCount;
  String acknowledgements[1];
  const std::size_t ackCount = pendingResultAck_.length() > 0 ? 1U : 0U;
  if (ackCount > 0) acknowledgements[0] = pendingResultAck_;
  QuickLabelPrintRequest quickLabelPrint;
  const QuickLabelPrintRequest* quickLabelPrintPtr = nullptr;
  if (pendingQuickLabelPrintId_.length() > 0) {
    quickLabelPrint.requestId = pendingQuickLabelPrintId_;
    quickLabelPrint.presetIndex = pendingQuickLabelPresetIndex_;
    quickLabelPrint.revision = pendingQuickLabelRevision_;
    quickLabelPrintPtr = &quickLabelPrint;
  }
  const auto requestId = clientConfig_.deviceId + String("-sync-") + String(syncSequence_++);
  const auto payload = buildSyncRequestJson(requestId, clientConfig_.deviceId, kFirmwareVersion, syncMode(),
                                            WiFi.RSSI(), outbox_.size(), events, eventCount,
                                            acknowledgements, ackCount, activeLookupId_, activeLookupCode_, quickLabelPrintPtr);
  if (queueSyncJob(requestId, payload)) {
    syncInFlight_ = true;
    lastSyncAttempt_ = millis();
  }
}

void InventatoryScanApp::processNetworkResult() {
  if (networkMutex_ == nullptr || xSemaphoreTake(networkMutex_, 0) != pdTRUE) return;
  if (!syncResultPending_) {
    xSemaphoreGive(networkMutex_);
    return;
  }
  const auto requestId = syncResultRequestId_;
  const auto sent = syncResultSent_;
  const auto status = syncResultStatus_;
  const auto body = syncResultBody_;
  syncResultPending_ = false;
  syncResultBody_.clear();
  xSemaphoreGive(networkMutex_);
  syncInFlight_ = false;

  if (!sent || status != 200) {
    pcOnline_ = false;
    Serial.print("POST /api/v1/device/sync failed, status ");
    Serial.println(status);
    // An authenticated 5xx means the desktop callback may already have
    // performed an idempotent print operation but could not fit its response
    // in the shared body budget. Keep the request for the next sync so the
    // cached desktop result can be delivered without printing twice.
    const bool retryableServerFailure = sent && status >= 500 && status <= 599;
    if (pendingQuickLabelPrintId_.length() > 0 && !retryableServerFailure) {
      pendingQuickLabelPrintId_.clear();
      pendingQuickLabelPresetIndex_ = 0;
      state_ = State::Idle;
      display_.showQuickLabelPrintResult(false, "PC OR PRINTER OFFLINE");
    }
    if ((status == 401 || status == 426) && !outbox_.empty()) {
      SyncResult result;
      result.resultId = "local-protocol";
      result.eventId = "local-protocol";
      result.status = "failed";
      result.code = status == 426 ? "UPDATE PC" : "PAIRING ERROR";
      result.message = status == 426 ? "Protocol v1 required" : "Check device token";
      display_.showResult(result);
    }
    return;
  }

  const auto response = parseSyncResponseJson(body);
  if (!response.valid || response.requestId != requestId) {
    pcOnline_ = false;
    Serial.println("Invalid protocol v1 sync response.");
    return;
  }
  pcOnline_ = true;
  if (pendingResultAck_.length() > 0) pendingResultAck_.clear();
  handleSyncResponse(response);
}

void InventatoryScanApp::handleSyncResponse(const SyncResponse& response) {
  if (response.hasQuickLabels) {
    quickLabelSnapshotReceived_ = true;
    pcSupportsQuickLabels_ = true;
    quickLabelPresetCount_ = 0;
    quickLabelRevision_ = response.quickLabelRevision;
    for (std::size_t index = 0; index < response.quickLabelPresetCount && index < kQuickLabelPresetLimit; ++index) {
      if (response.quickLabelPresets[index].length() > 0 &&
          response.quickLabelPresets[index].length() <= kQuickLabelPresetTextLimit) {
        quickLabelPresets_[quickLabelPresetCount_++] = response.quickLabelPresets[index];
      }
    }
    if (quickLabelPage_ * 4U >= quickLabelPresetCount_) quickLabelPage_ = 0;
    if (state_ == State::QuickLabels) {
      display_.showQuickLabels(quickLabelPresets_, quickLabelPresetCount_, quickLabelPage_,
                               quickLabelSnapshotReceived_, pcSupportsQuickLabels_);
    }
  } else if (state_ == State::QuickLabels) {
    // A normal protocol-v2 response without the optional extension means the paired desktop app is older.
    quickLabelSnapshotReceived_ = true;
    pcSupportsQuickLabels_ = false;
    display_.showQuickLabels(quickLabelPresets_, quickLabelPresetCount_, quickLabelPage_,
                             quickLabelSnapshotReceived_, pcSupportsQuickLabels_);
  }
  if (response.hasQuickLabelPrintResult && response.quickLabelPrintRequestId == pendingQuickLabelPrintId_) {
    const bool success = response.quickLabelPrintStatus == "completed";
    const auto message = response.quickLabelPrintMessage.length() > 0 ? response.quickLabelPrintMessage
                                                                       : response.quickLabelPrintCode;
    pendingQuickLabelPrintId_.clear();
    pendingQuickLabelPresetIndex_ = 0;
    state_ = State::Idle;
    display_.showQuickLabelPrintResult(success, message.length() > 0 ? message : "PRINT RESULT UNKNOWN");
  }
  while (!outbox_.empty()) {
    DeviceEventRecord front;
    if (!outbox_.peek(0, front)) break;
    bool accepted = false;
    for (std::size_t index = 0; index < response.acceptedCount; ++index) {
      if (response.acceptedEventIds[index] == front.eventId) {
        accepted = true;
        break;
      }
    }
    if (!accepted) break;
    if (!outbox_.pop()) {
      // Keep the event queued when its acknowledgement could not be made
      // durable; the next sync attempt can retry it without data loss.
      Serial.println("Outbox acknowledgement was not persisted; retaining event.");
      break;
    }
  }
  if (isCurrentLookupResult(response, activeLookupId_)) {
    lookupMissCount_ = 0;
    if (response.lookupStatus == "found" && response.lookupItemName.length() > 0) {
      const auto lookupText = response.lookupPurposeLabel.length() > 0
                                  ? response.lookupItemName + " | " + response.lookupPurposeLabel
                                  : response.lookupItemName;
      display_.updateQuantityLookup(lookupText, QuantityLookupState::Found);
      activeLookupId_.clear();
      activeLookupCode_.clear();
    } else if (response.lookupStatus == "not_found") {
      display_.updateQuantityLookup(pendingScanType_ == PendingScanType::ComponentDataMatrix
                                        ? String("Not in inventory")
                                        : String("Unknown inventory ID"),
                                    QuantityLookupState::NotFound);
      activeLookupId_.clear();
      activeLookupCode_.clear();
    } else if (response.lookupStatus == "unavailable") {
      display_.updateQuantityLookup("Lookup unavailable", QuantityLookupState::Unavailable);
      activeLookupId_.clear();
      activeLookupCode_.clear();
    }
  } else if (activeLookupId_.length() > 0 && ++lookupMissCount_ >= 3U) {
    display_.updateQuantityLookup("Lookup unavailable", QuantityLookupState::Unavailable);
    activeLookupId_.clear();
    activeLookupCode_.clear();
    lookupMissCount_ = 0;
  }
  updateConnectivityDisplay();
  if (response.hasResult) {
    pendingResultAck_ = response.result.resultId;
    lastSyncAttempt_ = 0;
    if (awaitingResultEventId_.length() == 0 || response.result.eventId == awaitingResultEventId_) {
      awaitingResultEventId_.clear();
      state_ = State::Idle;
      display_.showResult(response.result);
    }
  }
}

}  // namespace inventatory_scan