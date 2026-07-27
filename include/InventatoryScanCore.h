// Inventatory Scan R1 firmware core helpers.

#pragma once

#include <Arduino.h>

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace inventatory_scan {

inline String trimCopy(const String& value) {
  size_t begin = 0;
  while (begin < value.length() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
    ++begin;
  }

  size_t end = value.length();
  while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
    --end;
  }

  return value.substring(begin, end);
}

inline String stripAimSymbologyPrefix(const String& value) {
  if (value.length() >= 3U && value[0] == ']' && std::isalnum(static_cast<unsigned char>(value[1])) != 0 &&
      std::isalnum(static_cast<unsigned char>(value[2])) != 0) {
    return value.substring(3);
  }
  return value;
}

inline bool isStandardMachineCode(const String& value) {
  const auto code = trimCopy(value);
  if (code.length() != 4U) return false;
  for (size_t index = 0; index < code.length(); ++index) {
    if (!std::isdigit(static_cast<unsigned char>(code[index]))) return false;
  }
  return true;
}

inline bool isStandardManufacturerPartNumber(const String& value) {
  const auto code = trimCopy(value);
  if (code.length() < 2U || code.length() > 64U) return false;
  bool hasAlphanumeric = false;
  for (size_t index = 0; index < code.length(); ++index) {
    const auto ch = static_cast<unsigned char>(code[index]);
    if (std::isalnum(ch)) hasAlphanumeric = true;
    if (!std::isalnum(ch) && ch != '-' && ch != '.' && ch != '_' && ch != '/' && ch != '+' &&
        ch != '#' && ch != '&' && ch != ',' && ch != '(' && ch != ')' && ch != ' ') return false;
  }
  return hasAlphanumeric;
}

struct DecodedComponentFields {
  String manufacturer;
  String manufacturerPartNumber;
  String encodedPartName;
  String auxiliaryPartNumber;
  int quantity = 1;
};

inline bool isIso15434Separator(char ch) {
  const auto byte = static_cast<unsigned char>(ch);
  return byte == 0x1DU || byte == 0x1EU || byte == 0x04U;
}

inline bool iso15434Field(const String& field, const char* identifier, String& value) {
  const auto length = strlen(identifier);
  if (field.length() <= length || field.substring(0, length) != identifier) return false;
  value = trimCopy(field.substring(length));
  return value.length() > 0;
}

inline bool decodeComponentDataMatrix(const String& raw, DecodedComponentFields& decoded) {
  decoded = {};
  if (raw.length() == 0 || raw.length() > 512U) return false;
  const auto value = trimCopy(stripAimSymbologyPrefix(raw));
  if (!value.startsWith("[)>")) return false;
  size_t offset = 0;
  bool separated = false;
  if (value.length() >= 7U && static_cast<unsigned char>(value[3]) == 0x1EU && value[4] == '0' &&
      value[5] == '6' && static_cast<unsigned char>(value[6]) == 0x1DU) {
    offset = 7U;
    separated = true;
  } else if (value.length() >= 5U && value[3] == '0' && value[4] == '6') {
    offset = 5U;
  } else {
    return false;
  }

  String quantityText;
  if (separated) {
    size_t index = offset;
    while (index < value.length()) {
      while (index < value.length() && isIso15434Separator(value[index])) ++index;
      const auto begin = index;
      while (index < value.length() && !isIso15434Separator(value[index])) ++index;
      const auto field = value.substring(begin, index);
      String parsed;
      // Distributor labels put their own `P` identifier before the `1P`
      // manufacturer part number, so `P` never wins even when it is seen first.
      if (iso15434Field(field, "1P", parsed)) {
        if (decoded.manufacturerPartNumber.length() == 0) decoded.manufacturerPartNumber = parsed;
      } else if (iso15434Field(field, "P", parsed)) {
        if (decoded.auxiliaryPartNumber.length() == 0) decoded.auxiliaryPartNumber = parsed;
      } else if (decoded.manufacturer.length() == 0 && iso15434Field(field, "1V", parsed)) {
        decoded.manufacturer = parsed;
      } else if (decoded.manufacturer.length() == 0 && iso15434Field(field, "V", parsed)) {
        decoded.manufacturer = parsed;
      } else if (decoded.encodedPartName.length() == 0 && iso15434Field(field, "20P", parsed)) {
        decoded.encodedPartName = parsed;
      } else if (decoded.encodedPartName.length() == 0 && iso15434Field(field, "N", parsed)) {
        decoded.encodedPartName = parsed;
      } else if (quantityText.length() == 0 && iso15434Field(field, "Q", parsed)) {
        quantityText = parsed;
      }
    }
  } else {
    // A few scanner profiles suppress separators. Recover only fields whose
    // boundaries are unambiguous; uncertain values remain empty.
    const auto manufacturerMarker = value.indexOf("1P", offset);
    if (manufacturerMarker >= 0) {
      const auto begin = static_cast<size_t>(manufacturerMarker) + 2U;
      size_t end = value.length();
      for (size_t index = begin; index + 1U < value.length(); ++index) {
        if (value[index] == 'Q' && std::isdigit(static_cast<unsigned char>(value[index + 1U]))) {
          end = index;
          break;
        }
      }
      if (end > begin) decoded.manufacturerPartNumber = trimCopy(value.substring(begin, end));
      if (offset < static_cast<size_t>(manufacturerMarker) && value[offset] == 'P') {
        decoded.auxiliaryPartNumber = trimCopy(value.substring(offset + 1U, manufacturerMarker));
      }
    }
    for (size_t index = offset; index + 1U < value.length(); ++index) {
      if (value[index] != 'Q' || !std::isdigit(static_cast<unsigned char>(value[index + 1U]))) continue;
      size_t end = index + 1U;
      while (end < value.length() && std::isdigit(static_cast<unsigned char>(value[end]))) ++end;
      quantityText = value.substring(index + 1U, end);
      break;
    }
  }
  if (quantityText.length() > 0) {
    bool numeric = true;
    for (size_t index = 0; index < quantityText.length(); ++index) {
      if (!std::isdigit(static_cast<unsigned char>(quantityText[index]))) numeric = false;
    }
    if (numeric && quantityText.toInt() > 0) decoded.quantity = static_cast<int>(quantityText.toInt());
  }
  if (decoded.manufacturerPartNumber.length() > 0 &&
      !isStandardManufacturerPartNumber(decoded.manufacturerPartNumber)) decoded.manufacturerPartNumber.clear();
  return decoded.manufacturerPartNumber.length() > 0 || decoded.encodedPartName.length() > 0;
}

inline String hexByte(unsigned char value) {
  static const char kHex[] = "0123456789abcdef";
  String out;
  out += kHex[(value >> 4U) & 0x0fU];
  out += kHex[value & 0x0fU];
  return out;
}

inline String escapeJson(const String& value) {
  String out;
  out.reserve(value.length() * 2U);
  for (size_t i = 0; i < value.length(); ++i) {
    const unsigned char ch = static_cast<unsigned char>(value[i]);
    switch (ch) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (ch < 0x20U) {
          out += "\\u00";
          out += hexByte(ch);
        } else {
          out += static_cast<char>(ch);
        }
        break;
    }
  }
  return out;
}

inline String makeRequestId(const String& deviceId, unsigned long sequence) {
  String out = trimCopy(deviceId);
  out += '-';
  out += String(sequence);
  return out;
}

class QuantityComposer {
 public:
  void clear() {
    digits_.clear();
    defaultValue_ = 1;
    cursor_ = 0;
    editingStarted_ = false;
  }

  bool empty() const {
    return digits_.length() == 0;
  }

  void appendDigit(char digit) {
    if (digit < '0' || digit > '9') {
      return;
    }
    if (!editingStarted_) {
      digits_ = String(digit);
      cursor_ = 1;
      editingStarted_ = true;
      return;
    }
    ensureDigits();
    if (cursor_ < digits_.length()) {
      digits_.setCharAt(cursor_, digit);
    } else if (digits_.length() < 4) {
      digits_ += digit;
    }
    advanceCursor();
  }

  void setDefaultValue(int value) {
    defaultValue_ = value <= 0 ? 1 : value;
    digits_ = String(defaultValue_);
    if (digits_.length() > 4) digits_.remove(4);
    cursor_ = 0;
    editingStarted_ = false;
  }

  void cycleCursor() {
    ensureDigits();
    editingStarted_ = true;
    const auto slots = editableSlots();
    cursor_ = slots == 0 ? 0 : (cursor_ + 1U) % slots;
  }

  void deleteDigit() {
    ensureDigits();
    editingStarted_ = true;
    std::size_t deleteAt = cursor_;
    if (deleteAt >= digits_.length() && deleteAt > 0) --deleteAt;
    if (deleteAt < digits_.length()) digits_.remove(deleteAt, 1);
    if (digits_.length() == 0) digits_ = "1";
    cursor_ = min(deleteAt, digits_.length());
  }

  std::size_t cursor() const { return cursor_; }

  int valueOrOne() const {
    if (digits_.length() == 0) {
      return defaultValue_;
    }
    const long parsed = digits_.toInt();
    return parsed <= 0 ? defaultValue_ : static_cast<int>(parsed);
  }

  String displayText() const {
    return digits_.length() == 0 ? String(defaultValue_) : digits_;
  }

  int consume(bool add) {
    const auto value = valueOrOne();
    clear();
    return add ? value : -value;
  }

 private:
  void ensureDigits() {
    if (digits_.length() == 0) digits_ = String(defaultValue_);
    if (cursor_ >= editableSlots()) cursor_ = 0;
  }

  std::size_t editableSlots() const {
    return digits_.length() < 4 ? digits_.length() + 1U : digits_.length();
  }

  void advanceCursor() {
    const auto slots = editableSlots();
    if (slots == 0) {
      cursor_ = 0;
    } else if (cursor_ + 1U < slots) {
      ++cursor_;
    } else {
      cursor_ = 0;
    }
  }

  String digits_;
  int defaultValue_ = 1;
  std::size_t cursor_ = 0;
  bool editingStarted_ = false;
};

struct ScanRequest {
  String deviceId;
  String requestId;
  uint32_t sequence = 0;
  String code;
  int quantity = 1;
};

inline String buildQuantityRequestJson(const String& deviceId, const String& requestId, const String& code, int delta) {
  String out;
  out.reserve(deviceId.length() + requestId.length() + code.length() + 48U);
  out += "{\"deviceId\":\"";
  out += escapeJson(trimCopy(deviceId));
  out += "\",\"requestId\":\"";
  out += escapeJson(trimCopy(requestId));
  out += "\",\"code\":\"";
  out += escapeJson(trimCopy(code));
  out += "\",\"delta\":";
  out += String(delta);
  out += '}';
  return out;
}

inline String buildScanRequestJson(const String& deviceId, const String& requestId, const String& code,
                                  int quantity = 1) {
  String out;
  out.reserve(deviceId.length() + requestId.length() + code.length() + 56U);
  out += "{\"deviceId\":\"";
  out += escapeJson(trimCopy(deviceId));
  out += "\",\"requestId\":\"";
  out += escapeJson(trimCopy(requestId));
  out += "\",\"code\":\"";
  out += escapeJson(trimCopy(code));
  out += "\",\"quantity\":";
  out += String(quantity);
  out += '}';
  return out;
}

inline String buildDebugRequestJson(const String& deviceId, const String& requestId, const String& level,
                                    const String& message) {
  String out;
  out.reserve(deviceId.length() + requestId.length() + level.length() + message.length() + 48U);
  out += "{\"deviceId\":\"";
  out += escapeJson(trimCopy(deviceId));
  out += "\",\"requestId\":\"";
  out += escapeJson(trimCopy(requestId));
  out += "\",\"level\":\"";
  out += escapeJson(trimCopy(level));
  out += "\",\"message\":\"";
  out += escapeJson(trimCopy(message));
  out += "\"}";
  return out;
}

inline String buildStatusRequestJson(const String& deviceId, const String& firmwareVersion, int rssi,
                                     const String& debug = String()) {
  String out;
  out.reserve(deviceId.length() + firmwareVersion.length() + debug.length() + 48U);
  out += "{\"deviceId\":\"";
  out += escapeJson(trimCopy(deviceId));
  out += "\",\"firmwareVersion\":\"";
  out += escapeJson(trimCopy(firmwareVersion));
  out += "\",\"rssi\":";
  out += String(rssi);
  out += ",\"debug\":\"";
  out += escapeJson(trimCopy(debug));
  out += "\"";
  out += '}';
  return out;
}

}  // namespace inventatory_scan
