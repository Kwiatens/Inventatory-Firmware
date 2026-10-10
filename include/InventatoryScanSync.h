#pragma once

#include <Arduino.h>

#include <climits>
#include <cstdint>

#include "InventatoryScanCore.h"
#include "InventatoryScanHttp.h"
#include "InventatoryScanSecurity.h"
#include "storage/outbox_store.h"

namespace inventatory_scan {

constexpr std::size_t kSyncBatchSize = 4;
constexpr std::size_t kQuickLabelPresetLimit = 12;
constexpr std::size_t kQuickLabelPresetTextLimit = 24;

struct QuickLabelPrintRequest {
  String requestId;
  uint8_t presetIndex = 0;  // One-based index in the ordered PC preset list.
  uint32_t revision = 0;
};

struct SyncResult {
  String resultId;
  String eventId;
  String status;
  bool existing = false;
  String itemName;
  String purposeLabel;
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
  String lookupPurposeLabel;
  bool hasQuickLabels = false;
  uint32_t quickLabelRevision = 0;
  String quickLabelPresets[kQuickLabelPresetLimit];
  std::size_t quickLabelPresetCount = 0;
  bool hasQuickLabelPrintResult = false;
  String quickLabelPrintRequestId;
  String quickLabelPrintStatus;
  String quickLabelPrintCode;
  String quickLabelPrintMessage;
};

inline bool isInventoryResultStatus(const String& status) {
  return status == "completed" || status == "completed_with_warning" || status == "failed";
}

inline bool isQuickLabelPrintStatus(const String& status) {
  return status == "completed" || status == "failed";
}

inline const char* syncEventType(DeviceEventKind kind) {
  return kind == DeviceEventKind::InventoryReceive ? "inventory.receive" : "inventory.adjust";
}

inline String buildSyncRequestJson(const String& requestId, const String& deviceId,
                                   const String& firmwareVersion, const String& mode, int rssi,
                                   std::size_t queueDepth, const DeviceEventRecord* events,
                                   std::size_t eventCount, const String* resultAcks,
                                   std::size_t resultAckCount,
                                   const String& lookupId = String(),
                                   const String& lookupCode = String(),
                                   const QuickLabelPrintRequest* quickLabelPrint = nullptr) {
  String out;
  out.reserve(512);
  out += "{\"protocolVersion\":";
  out += String(kInventatoryScanProtocolVersion);
  out += ",\"requestId\":\"";
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
  out += ",\"capabilities\":[\"lcd.128x64\",\"keypad.4x4\",\"scanner.gm65\",\"decoded_component.v2\",\"quick_labels.v1\"],\"events\":[";
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
    if (events[index].manufacturer.length() > 0 || events[index].manufacturerPartNumber.length() > 0 ||
        events[index].encodedPartName.length() > 0) {
      out += ",\"component\":{\"manufacturer\":\"";
      out += escapeJson(events[index].manufacturer);
      out += "\",\"manufacturerPartNumber\":\"";
      out += escapeJson(events[index].manufacturerPartNumber);
      out += "\",\"encodedPartName\":\"";
      out += escapeJson(events[index].encodedPartName);
      out += "\"}";
    }
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
  if (quickLabelPrint != nullptr && quickLabelPrint->requestId.length() > 0 &&
      quickLabelPrint->presetIndex > 0 && quickLabelPrint->revision > 0) {
    out += ",\"quickLabelPrint\":{\"requestId\":\"";
    out += escapeJson(quickLabelPrint->requestId);
    out += "\",\"presetIndex\":";
    out += String(quickLabelPrint->presetIndex);
    out += ",\"revision\":";
    out += String(quickLabelPrint->revision);
    out += '}';
  }
  out += '}';
  return out;
}

enum class JsonValueKind : uint8_t {
  Invalid,
  Object,
  Array,
  String,
  Number,
  True,
  False,
  Null,
};

struct JsonValueView {
  std::size_t start = 0;
  std::size_t end = 0;
  JsonValueKind kind = JsonValueKind::Invalid;
};

class SyncJsonParser {
 public:
  explicit SyncJsonParser(const String& input) : input_(input) {}

  bool parseDocument(JsonValueView& root) const {
    std::size_t position = 0;
    skipWhitespace(position);
    if (!parseValue(position, 0, root) || root.kind != JsonValueKind::Object) return false;
    skipWhitespace(position);
    return position == input_.length();
  }

  bool objectMember(const JsonValueView& object, const char* name, JsonValueView& value, bool& found) const {
    if (object.kind != JsonValueKind::Object || name == nullptr || object.end <= object.start + 1U) return false;
    found = false;
    std::size_t position = object.start + 1U;
    skipWhitespace(position);
    if (position < object.end && input_[position] == '}') return position + 1U == object.end;

    while (position < object.end) {
      String memberName;
      if (!parseString(position, &memberName)) return false;
      skipWhitespace(position);
      if (position >= object.end || input_[position++] != ':') return false;
      skipWhitespace(position);

      JsonValueView memberValue;
      if (!parseValue(position, 1, memberValue)) return false;
      if (memberName == name) {
        if (found) return false;
        found = true;
        value = memberValue;
      }

      skipWhitespace(position);
      if (position >= object.end) return false;
      if (input_[position] == '}') return position + 1U == object.end;
      if (input_[position++] != ',') return false;
      skipWhitespace(position);
    }
    return false;
  }

  bool arrayItems(const JsonValueView& array, JsonValueKind itemKind, JsonValueView* items,
                  std::size_t capacity, std::size_t& count) const {
    if (array.kind != JsonValueKind::Array || items == nullptr || capacity == 0 || array.end <= array.start + 1U) {
      return false;
    }
    count = 0;
    std::size_t position = array.start + 1U;
    skipWhitespace(position);
    if (position < array.end && input_[position] == ']') return position + 1U == array.end;

    while (position < array.end) {
      if (count >= capacity) return false;
      JsonValueView item;
      if (!parseValue(position, 1, item) || item.kind != itemKind) return false;
      items[count++] = item;

      skipWhitespace(position);
      if (position >= array.end) return false;
      if (input_[position] == ']') return position + 1U == array.end;
      if (input_[position++] != ',') return false;
      skipWhitespace(position);
    }
    return false;
  }

  bool readString(const JsonValueView& value, String& decoded) const {
    if (value.kind != JsonValueKind::String) return false;
    std::size_t position = value.start;
    if (!parseString(position, &decoded)) return false;
    return position == value.end;
  }

  bool readInteger(const JsonValueView& value, int& decoded) const {
    if (value.kind != JsonValueKind::Number) return false;
    std::size_t position = value.start;
    bool negative = false;
    if (position < value.end && input_[position] == '-') {
      negative = true;
      ++position;
    }
    if (position >= value.end) return false;

    const uint64_t limit = negative ? static_cast<uint64_t>(INT_MAX) + 1U : static_cast<uint64_t>(INT_MAX);
    uint64_t magnitude = 0;
    for (; position < value.end; ++position) {
      const char ch = input_[position];
      if (ch < '0' || ch > '9') return false;
      const auto digit = static_cast<uint64_t>(ch - '0');
      if (magnitude > (limit - digit) / 10U) return false;
      magnitude = magnitude * 10U + digit;
    }

    if (negative) {
      decoded = magnitude == static_cast<uint64_t>(INT_MAX) + 1U ? INT_MIN : -static_cast<int>(magnitude);
    } else {
      decoded = static_cast<int>(magnitude);
    }
    return true;
  }

  bool readBoolean(const JsonValueView& value, bool& decoded) const {
    if (value.kind == JsonValueKind::True) {
      decoded = true;
      return true;
    }
    if (value.kind == JsonValueKind::False) {
      decoded = false;
      return true;
    }
    return false;
  }

 private:
  static constexpr unsigned kMaxDepth = 16;

  void skipWhitespace(std::size_t& position) const {
    while (position < input_.length()) {
      const char ch = input_[position];
      if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') break;
      ++position;
    }
  }

  bool parseValue(std::size_t& position, unsigned depth, JsonValueView& value) const {
    if (depth > kMaxDepth || position >= input_.length()) return false;
    value.start = position;

    const char first = input_[position];
    if (first == '{') {
      value.kind = JsonValueKind::Object;
      if (!parseObject(position, depth + 1U)) return false;
    } else if (first == '[') {
      value.kind = JsonValueKind::Array;
      if (!parseArray(position, depth + 1U)) return false;
    } else if (first == '"') {
      value.kind = JsonValueKind::String;
      if (!parseString(position, nullptr)) return false;
    } else if (first == '-' || (first >= '0' && first <= '9')) {
      value.kind = JsonValueKind::Number;
      if (!parseNumber(position)) return false;
    } else if (parseLiteral(position, "true")) {
      value.kind = JsonValueKind::True;
    } else if (parseLiteral(position, "false")) {
      value.kind = JsonValueKind::False;
    } else if (parseLiteral(position, "null")) {
      value.kind = JsonValueKind::Null;
    } else {
      return false;
    }

    value.end = position;
    return true;
  }

  bool parseObject(std::size_t& position, unsigned childDepth) const {
    ++position;
    skipWhitespace(position);
    if (position < input_.length() && input_[position] == '}') {
      ++position;
      return true;
    }

    while (position < input_.length()) {
      if (!parseString(position, nullptr)) return false;
      skipWhitespace(position);
      if (position >= input_.length() || input_[position++] != ':') return false;
      skipWhitespace(position);

      JsonValueView value;
      if (!parseValue(position, childDepth, value)) return false;
      skipWhitespace(position);
      if (position >= input_.length()) return false;
      if (input_[position] == '}') {
        ++position;
        return true;
      }
      if (input_[position++] != ',') return false;
      skipWhitespace(position);
    }
    return false;
  }

  bool parseArray(std::size_t& position, unsigned childDepth) const {
    ++position;
    skipWhitespace(position);
    if (position < input_.length() && input_[position] == ']') {
      ++position;
      return true;
    }

    while (position < input_.length()) {
      JsonValueView value;
      if (!parseValue(position, childDepth, value)) return false;
      skipWhitespace(position);
      if (position >= input_.length()) return false;
      if (input_[position] == ']') {
        ++position;
        return true;
      }
      if (input_[position++] != ',') return false;
      skipWhitespace(position);
    }
    return false;
  }

  bool parseString(std::size_t& position, String* decoded) const {
    if (position >= input_.length() || input_[position++] != '"') return false;
    if (decoded != nullptr) decoded->clear();

    while (position < input_.length()) {
      const unsigned char ch = static_cast<unsigned char>(input_[position++]);
      if (ch == '"') return true;
      if (ch < 0x20U) return false;
      if (ch != '\\') {
        if (ch < 0x80U) {
          if (decoded != nullptr) *decoded += static_cast<char>(ch);
        } else if (!appendUtf8Sequence(position, ch, decoded)) {
          return false;
        }
        continue;
      }
      if (position >= input_.length()) return false;

      const char escape = input_[position++];
      switch (escape) {
        case '"': if (decoded != nullptr) *decoded += '"'; break;
        case '\\': if (decoded != nullptr) *decoded += '\\'; break;
        case '/': if (decoded != nullptr) *decoded += '/'; break;
        case 'b': if (decoded != nullptr) *decoded += '\b'; break;
        case 'f': if (decoded != nullptr) *decoded += '\f'; break;
        case 'n': if (decoded != nullptr) *decoded += '\n'; break;
        case 'r': if (decoded != nullptr) *decoded += '\r'; break;
        case 't': if (decoded != nullptr) *decoded += '\t'; break;
        case 'u': {
          uint32_t codePoint = 0;
          if (!readHexQuad(position, codePoint)) return false;
          if (codePoint >= 0xD800U && codePoint <= 0xDBFFU) {
            if (position + 2U > input_.length() || input_[position] != '\\' || input_[position + 1U] != 'u') {
              return false;
            }
            position += 2U;
            uint32_t low = 0;
            if (!readHexQuad(position, low) || low < 0xDC00U || low > 0xDFFFU) return false;
            codePoint = 0x10000U + ((codePoint - 0xD800U) << 10U) + (low - 0xDC00U);
          } else if (codePoint >= 0xDC00U && codePoint <= 0xDFFFU) {
            return false;
          }
          if (decoded != nullptr && !appendCodePoint(*decoded, codePoint)) return false;
          break;
        }
        default: return false;
      }
    }
    return false;
  }

  bool appendUtf8Sequence(std::size_t& position, unsigned char first, String* decoded) const {
    unsigned continuationCount = 0;
    uint32_t codePoint = 0;
    uint32_t minimumValue = 0;
    if (first >= 0xC2U && first <= 0xDFU) {
      continuationCount = 1;
      codePoint = first & 0x1FU;
      minimumValue = 0x80U;
    } else if (first >= 0xE0U && first <= 0xEFU) {
      continuationCount = 2;
      codePoint = first & 0x0FU;
      minimumValue = 0x800U;
    } else if (first >= 0xF0U && first <= 0xF4U) {
      continuationCount = 3;
      codePoint = first & 0x07U;
      minimumValue = 0x10000U;
    } else {
      return false;
    }

    const auto sequenceStart = position - 1U;
    for (unsigned index = 0; index < continuationCount; ++index) {
      if (position >= input_.length()) return false;
      const auto next = static_cast<unsigned char>(input_[position++]);
      if ((next & 0xC0U) != 0x80U) return false;
      codePoint = (codePoint << 6U) | (next & 0x3FU);
    }
    if (codePoint < minimumValue || codePoint > 0x10FFFFU ||
        (codePoint >= 0xD800U && codePoint <= 0xDFFFU)) {
      return false;
    }

    if (decoded != nullptr) {
      for (std::size_t index = sequenceStart; index < position; ++index) *decoded += input_[index];
    }
    return true;
  }

  bool readHexQuad(std::size_t& position, uint32_t& value) const {
    if (position + 4U > input_.length()) return false;
    value = 0;
    for (unsigned index = 0; index < 4; ++index) {
      const char ch = input_[position++];
      uint32_t digit = 0;
      if (ch >= '0' && ch <= '9') digit = static_cast<uint32_t>(ch - '0');
      else if (ch >= 'a' && ch <= 'f') digit = static_cast<uint32_t>(ch - 'a' + 10);
      else if (ch >= 'A' && ch <= 'F') digit = static_cast<uint32_t>(ch - 'A' + 10);
      else return false;
      value = (value << 4U) | digit;
    }
    return true;
  }

  static bool appendCodePoint(String& output, uint32_t codePoint) {
    if (codePoint == 0) return false;
    if (codePoint <= 0x7FU) {
      output += static_cast<char>(codePoint);
    } else if (codePoint <= 0x7FFU) {
      output += static_cast<char>(0xC0U | (codePoint >> 6U));
      output += static_cast<char>(0x80U | (codePoint & 0x3FU));
    } else if (codePoint <= 0xFFFFU) {
      output += static_cast<char>(0xE0U | (codePoint >> 12U));
      output += static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU));
      output += static_cast<char>(0x80U | (codePoint & 0x3FU));
    } else if (codePoint <= 0x10FFFFU) {
      output += static_cast<char>(0xF0U | (codePoint >> 18U));
      output += static_cast<char>(0x80U | ((codePoint >> 12U) & 0x3FU));
      output += static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU));
      output += static_cast<char>(0x80U | (codePoint & 0x3FU));
    } else {
      return false;
    }
    return true;
  }

  bool parseNumber(std::size_t& position) const {
    if (input_[position] == '-') ++position;
    if (position >= input_.length()) return false;
    if (input_[position] == '0') {
      ++position;
      if (position < input_.length() && input_[position] >= '0' && input_[position] <= '9') return false;
    } else {
      if (input_[position] < '1' || input_[position] > '9') return false;
      while (position < input_.length() && input_[position] >= '0' && input_[position] <= '9') ++position;
    }

    if (position < input_.length() && input_[position] == '.') {
      ++position;
      const auto fractionStart = position;
      while (position < input_.length() && input_[position] >= '0' && input_[position] <= '9') ++position;
      if (position == fractionStart) return false;
    }
    if (position < input_.length() && (input_[position] == 'e' || input_[position] == 'E')) {
      ++position;
      if (position < input_.length() && (input_[position] == '+' || input_[position] == '-')) ++position;
      const auto exponentStart = position;
      while (position < input_.length() && input_[position] >= '0' && input_[position] <= '9') ++position;
      if (position == exponentStart) return false;
    }
    return true;
  }

  bool parseLiteral(std::size_t& position, const char* literal) const {
    std::size_t index = position;
    while (*literal != '\0') {
      if (index >= input_.length() || input_[index++] != *literal++) return false;
    }
    position = index;
    return true;
  }

  const String& input_;
};

inline bool jsonStringField(const SyncJsonParser& parser, const JsonValueView& object, const char* key,
                            String& value, bool required) {
  JsonValueView field;
  bool found = false;
  if (!parser.objectMember(object, key, field, found)) return false;
  if (!found) {
    value.clear();
    return !required;
  }
  return parser.readString(field, value);
}

inline bool jsonIntegerField(const SyncJsonParser& parser, const JsonValueView& object, const char* key,
                             int& value, bool required) {
  JsonValueView field;
  bool found = false;
  if (!parser.objectMember(object, key, field, found)) return false;
  if (!found) {
    value = 0;
    return !required;
  }
  return parser.readInteger(field, value);
}

inline bool jsonBooleanField(const SyncJsonParser& parser, const JsonValueView& object, const char* key,
                             bool& value, bool required) {
  JsonValueView field;
  bool found = false;
  if (!parser.objectMember(object, key, field, found)) return false;
  if (!found) {
    value = false;
    return !required;
  }
  return parser.readBoolean(field, value);
}

inline bool isLookupResultStatus(const String& status) {
  return status == "found" || status == "not_found" || status == "unavailable";
}

inline bool parseSyncResult(const SyncJsonParser& parser, const JsonValueView& object, SyncResult& result) {
  if (!jsonStringField(parser, object, "resultId", result.resultId, true) ||
      !jsonStringField(parser, object, "eventId", result.eventId, true) ||
      !jsonStringField(parser, object, "status", result.status, true) ||
      !jsonBooleanField(parser, object, "existing", result.existing, false) ||
      !jsonStringField(parser, object, "itemName", result.itemName, false) ||
      !jsonStringField(parser, object, "purposeLabel", result.purposeLabel, false) ||
      !jsonIntegerField(parser, object, "requestedDelta", result.requestedDelta, false) ||
      !jsonIntegerField(parser, object, "appliedDelta", result.appliedDelta, false) ||
      !jsonIntegerField(parser, object, "quantity", result.quantity, false) ||
      !jsonStringField(parser, object, "location", result.location, false) ||
      !jsonStringField(parser, object, "code", result.code, false) ||
      !jsonStringField(parser, object, "message", result.message, false)) {
    return false;
  }
  return result.resultId.length() > 0 && result.eventId.length() > 0 &&
         isInventoryResultStatus(result.status);
}

// An optional member serialised as JSON null is treated exactly as absent. Any other
// present kind is still passed to the caller's kind check and rejected there.
inline bool isPresentOptionalMember(const JsonValueView& member, bool found) {
  return found && member.kind != JsonValueKind::Null;
}

inline SyncResponse parseSyncResponseJson(const String& body) {
  SyncResponse response;
  if (body.length() > kMaxHttpBodyBytes) return response;

  SyncJsonParser parser(body);
  JsonValueView root;
  if (!parser.parseDocument(root)) return response;

  JsonValueView field;
  bool found = false;
  int protocolVersion = 0;
  if (!jsonIntegerField(parser, root, "protocolVersion", protocolVersion, true) ||
      protocolVersion != kInventatoryScanProtocolVersion ||
      !jsonStringField(parser, root, "requestId", response.requestId, true) ||
      response.requestId.length() == 0) {
    return response;
  }

  JsonValueView acceptedValues[kSyncBatchSize];
  std::size_t acceptedCount = 0;
  if (!parser.objectMember(root, "acceptedEventIds", field, found) || !found ||
      !parser.arrayItems(field, JsonValueKind::String, acceptedValues, kSyncBatchSize, acceptedCount)) {
    return response;
  }
  for (std::size_t index = 0; index < acceptedCount; ++index) {
    if (!parser.readString(acceptedValues[index], response.acceptedEventIds[index]) ||
        response.acceptedEventIds[index].length() == 0) {
      return response;
    }
    for (std::size_t previous = 0; previous < index; ++previous) {
      if (response.acceptedEventIds[previous] == response.acceptedEventIds[index]) return response;
    }
  }
  response.acceptedCount = acceptedCount;

  JsonValueView resultValues[kSyncBatchSize];
  std::size_t resultCount = 0;
  if (!parser.objectMember(root, "results", field, found) || !found ||
      !parser.arrayItems(field, JsonValueKind::Object, resultValues, kSyncBatchSize, resultCount)) {
    return response;
  }
  for (std::size_t index = 0; index < resultCount; ++index) {
    SyncResult result;
    if (!parseSyncResult(parser, resultValues[index], result)) return response;
    if (index == 0) response.result = result;
  }
  response.hasResult = resultCount > 0;

  if (!parser.objectMember(root, "lookupResult", field, found)) return response;
  if (isPresentOptionalMember(field, found)) {
    if (field.kind != JsonValueKind::Object ||
        !jsonStringField(parser, field, "lookupId", response.lookupId, true) ||
        !jsonStringField(parser, field, "status", response.lookupStatus, true) ||
        !jsonStringField(parser, field, "itemName", response.lookupItemName, false) ||
        !jsonStringField(parser, field, "purposeLabel", response.lookupPurposeLabel, false) ||
        response.lookupId.length() == 0 || !isLookupResultStatus(response.lookupStatus)) {
      return response;
    }
    response.hasLookupResult = true;
  }

  JsonValueView quickLabelsObject;
  if (!parser.objectMember(root, "quickLabels", quickLabelsObject, found)) return response;
  if (isPresentOptionalMember(quickLabelsObject, found)) {
    int revision = 0;
    JsonValueView presetsArray;
    JsonValueView presetValues[kQuickLabelPresetLimit];
    std::size_t presetCount = 0;
    if (quickLabelsObject.kind != JsonValueKind::Object ||
        !jsonIntegerField(parser, quickLabelsObject, "revision", revision, true) || revision <= 0 ||
        !parser.objectMember(quickLabelsObject, "presets", presetsArray, found) || !found ||
        !parser.arrayItems(presetsArray, JsonValueKind::String, presetValues, kQuickLabelPresetLimit, presetCount)) {
      return response;
    }
    for (std::size_t index = 0; index < presetCount; ++index) {
      if (!parser.readString(presetValues[index], response.quickLabelPresets[index]) ||
          response.quickLabelPresets[index].length() == 0 ||
          response.quickLabelPresets[index].length() > kQuickLabelPresetTextLimit) {
        return response;
      }
    }
    response.quickLabelPresetCount = presetCount;
    response.quickLabelRevision = static_cast<uint32_t>(revision);
    response.hasQuickLabels = true;
  }

  JsonValueView quickLabelPrint;
  if (!parser.objectMember(root, "quickLabelPrintResult", quickLabelPrint, found)) return response;
  if (isPresentOptionalMember(quickLabelPrint, found)) {
    if (quickLabelPrint.kind != JsonValueKind::Object ||
        !jsonStringField(parser, quickLabelPrint, "requestId", response.quickLabelPrintRequestId, true) ||
        !jsonStringField(parser, quickLabelPrint, "status", response.quickLabelPrintStatus, true) ||
        !jsonStringField(parser, quickLabelPrint, "code", response.quickLabelPrintCode, false) ||
        !jsonStringField(parser, quickLabelPrint, "message", response.quickLabelPrintMessage, false) ||
        response.quickLabelPrintRequestId.length() == 0 ||
        !isQuickLabelPrintStatus(response.quickLabelPrintStatus)) {
      return response;
    }
    response.hasQuickLabelPrintResult = true;
  }

  response.valid = true;
  return response;
}

inline bool isCurrentLookupResult(const SyncResponse& response, const String& activeLookupId) {
  return response.hasLookupResult && activeLookupId.length() > 0 && response.lookupId == activeLookupId;
}

}  // namespace inventatory_scan
