// HIMS Scan R1 firmware core helpers.

#pragma once

#include <Arduino.h>

#include <cctype>
#include <cstddef>
#include <cstdint>

namespace hims_scan {

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
  }

  bool empty() const {
    return digits_.length() == 0;
  }

  void appendDigit(char digit) {
    if (digit < '0' || digit > '9') {
      return;
    }
    if (digits_.length() >= 4) {
      return;
    }
    digits_ += digit;
  }

  void setDefaultValue(int value) {
    defaultValue_ = value <= 0 ? 1 : value;
  }

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
  String digits_;
  int defaultValue_ = 1;
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

}  // namespace hims_scan
