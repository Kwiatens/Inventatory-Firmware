#pragma once

#include <Arduino.h>

#include "HimsScanCore.h"
#include "storage/outbox_store.h"

namespace hims_scan {

constexpr int kHimsScanProtocolVersion = 1;
constexpr std::size_t kSyncBatchSize = 4;

struct SyncResult {
  String resultId;
  String eventId;
  String status;
  bool existing = false;
  String itemName;
  int requestedDelta = 0;
  int appliedDelta = 0;
  int quantity = 0;
  String location;
  String code;
  String message;
};

struct SyncResponse {
  bool valid = false;
  String requestId;
  String acceptedEventIds[kSyncBatchSize];
  std::size_t acceptedCount = 0;
  bool hasResult = false;
  SyncResult result;
  bool hasLookupResult = false;
  String lookupId;
  String lookupStatus;
  String lookupItemName;
};

inline const char* syncEventType(DeviceEventKind kind) {
  return kind == DeviceEventKind::InventoryReceive ? "inventory.receive" : "inventory.adjust";
}

inline String buildSyncRequestJson(const String& requestId, const String& deviceId,
                                   const String& firmwareVersion, const String& mode, int rssi,
                                   std::size_t queueDepth, const DeviceEventRecord* events,
                                   std::size_t eventCount, const String* resultAcks,
                                   std::size_t resultAckCount,
                                   const String& lookupId = String(),
                                   const String& lookupCode = String()) {
  String out;
  out.reserve(512);
  out += "{\"protocolVersion\":1,\"requestId\":\"";
  out += escapeJson(requestId);
  out += "\",\"deviceId\":\"";
  out += escapeJson(deviceId);
  out += "\",\"firmwareVersion\":\"";
  out += escapeJson(firmwareVersion);
  out += "\",\"mode\":\"";
  out += escapeJson(mode);
  out += "\",\"rssi\":";
  out += String(rssi);
  out += ",\"queueDepth\":";
  out += String(queueDepth);
  out += ",\"capabilities\":[\"lcd.128x64\",\"keypad.4x4\",\"scanner.gm65\",\"lookup.machine_code_name\"],\"events\":[";
  for (std::size_t index = 0; index < eventCount; ++index) {
    if (index != 0) out += ',';
    out += "{\"eventId\":\"";
    out += escapeJson(events[index].eventId);
    out += "\",\"type\":\"";
    out += syncEventType(events[index].kind);
    out += "\",\"code\":\"";
    out += escapeJson(events[index].code);
    out += "\",\"value\":";
    out += String(events[index].value);
    out += '}';
  }
  out += "],\"resultAcks\":[";
  for (std::size_t index = 0; index < resultAckCount; ++index) {
    if (index != 0) out += ',';
    out += '"';
    out += escapeJson(resultAcks[index]);
    out += '"';
  }
  out += ']';
  if (lookupId.length() > 0 && lookupCode.length() > 0) {
    out += ",\"lookup\":{\"lookupId\":\"";
    out += escapeJson(lookupId);
    out += "\",\"code\":\"";
    out += escapeJson(lookupCode);
    out += "\"}";
  }
  out += '}';
  return out;
}

inline int jsonValueStart(const String& body, const char* key) {
  const String marker = String('"') + key + '"';
  const int keyAt = body.indexOf(marker);
  if (keyAt < 0) return -1;
  const int colon = body.indexOf(':', keyAt + marker.length());
  if (colon < 0) return -1;
  int valueAt = colon + 1;
  while (valueAt < static_cast<int>(body.length()) && isspace(static_cast<unsigned char>(body[valueAt]))) ++valueAt;
  return valueAt;
}

inline String jsonStringValue(const String& body, const char* key) {
  int position = jsonValueStart(body, key);
  if (position < 0 || body[position] != '"') return {};
  String value;
  bool escaped = false;
  for (++position; position < static_cast<int>(body.length()); ++position) {
    const char ch = body[position];
    if (escaped) {
      if (ch == 'n') value += '\n';
      else if (ch == 'r') value += '\r';
      else if (ch == 't') value += '\t';
      else value += ch;
      escaped = false;
    } else if (ch == '\\') {
      escaped = true;
    } else if (ch == '"') {
      return value;
    } else {
      value += ch;
    }
  }
  return {};
}

inline int jsonIntValue(const String& body, const char* key) {
  const int position = jsonValueStart(body, key);
  return position < 0 ? 0 : body.substring(position).toInt();
}

inline bool jsonBoolValue(const String& body, const char* key) {
  const int position = jsonValueStart(body, key);
  return position >= 0 && body.substring(position).startsWith("true");
}

inline String jsonArrayValue(const String& body, const char* key) {
  const int position = jsonValueStart(body, key);
  if (position < 0 || body[position] != '[') return {};
  bool inString = false;
  bool escaped = false;
  int depth = 0;
  for (int index = position; index < static_cast<int>(body.length()); ++index) {
    const char ch = body[index];
    if (inString) {
      if (escaped) escaped = false;
      else if (ch == '\\') escaped = true;
      else if (ch == '"') inString = false;
      continue;
    }
    if (ch == '"') inString = true;
    else if (ch == '[') ++depth;
    else if (ch == ']' && --depth == 0) return body.substring(position + 1, index);
  }
  return {};
}

inline String jsonObjectValue(const String& body, const char* key) {
  const int position = jsonValueStart(body, key);
  if (position < 0 || body[position] != '{') return {};
  bool inString = false;
  bool escaped = false;
  int depth = 0;
  for (int index = position; index < static_cast<int>(body.length()); ++index) {
    const char ch = body[index];
    if (inString) {
      if (escaped) escaped = false;
      else if (ch == '\\') escaped = true;
      else if (ch == '"') inString = false;
      continue;
    }
    if (ch == '"') inString = true;
    else if (ch == '{') ++depth;
    else if (ch == '}' && --depth == 0) return body.substring(position, index + 1);
  }
  return {};
}

inline SyncResponse parseSyncResponseJson(const String& body) {
  SyncResponse response;
  if (jsonIntValue(body, "protocolVersion") != kHimsScanProtocolVersion) return response;
  response.requestId = jsonStringValue(body, "requestId");
  if (response.requestId.length() == 0) return response;

  const auto accepted = jsonArrayValue(body, "acceptedEventIds");
  int position = 0;
  while (position < static_cast<int>(accepted.length()) && response.acceptedCount < kSyncBatchSize) {
    const int begin = accepted.indexOf('"', position);
    if (begin < 0) break;
    const int end = accepted.indexOf('"', begin + 1);
    if (end < 0) break;
    response.acceptedEventIds[response.acceptedCount++] = accepted.substring(begin + 1, end);
    position = end + 1;
  }

  const auto results = jsonArrayValue(body, "results");
  const int objectBegin = results.indexOf('{');
  const int objectEnd = results.lastIndexOf('}');
  if (objectBegin >= 0 && objectEnd > objectBegin) {
    const auto object = results.substring(objectBegin, objectEnd + 1);
    response.result.resultId = jsonStringValue(object, "resultId");
    response.result.eventId = jsonStringValue(object, "eventId");
    response.result.status = jsonStringValue(object, "status");
    response.result.existing = jsonBoolValue(object, "existing");
    response.result.itemName = jsonStringValue(object, "itemName");
    response.result.requestedDelta = jsonIntValue(object, "requestedDelta");
    response.result.appliedDelta = jsonIntValue(object, "appliedDelta");
    response.result.quantity = jsonIntValue(object, "quantity");
    response.result.location = jsonStringValue(object, "location");
    response.result.code = jsonStringValue(object, "code");
    response.result.message = jsonStringValue(object, "message");
    response.hasResult = response.result.resultId.length() > 0 && response.result.eventId.length() > 0;
  }
  const auto lookup = jsonObjectValue(body, "lookupResult");
  if (lookup.length() > 0) {
    response.lookupId = jsonStringValue(lookup, "lookupId");
    response.lookupStatus = jsonStringValue(lookup, "status");
    response.lookupItemName = jsonStringValue(lookup, "itemName");
    response.hasLookupResult = response.lookupId.length() > 0 && response.lookupStatus.length() > 0;
  }
  response.valid = true;
  return response;
}

inline bool isCurrentLookupResult(const SyncResponse& response, const String& activeLookupId) {
  return response.hasLookupResult && activeLookupId.length() > 0 && response.lookupId == activeLookupId;
}

}  // namespace hims_scan
