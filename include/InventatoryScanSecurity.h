// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <Arduino.h>
#include <cstdint>
#include <mbedtls/md.h>

namespace inventatory_scan {

constexpr int kInventatoryScanProtocolVersion = 1;

inline int transportHexDigit(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return 10 + ch - 'a';
  if (ch >= 'A' && ch <= 'F') return 10 + ch - 'A';
  return -1;
}

inline bool decodeTransportToken(const String& token, uint8_t (&bytes)[32]) {
  if (token.length() != 64) return false;
  for (size_t index = 0; index < 32; ++index) {
    const auto high = transportHexDigit(token[index * 2U]);
    const auto low = transportHexDigit(token[index * 2U + 1U]);
    if (high < 0 || low < 0) return false;
    bytes[index] = static_cast<uint8_t>((high << 4U) | low);
  }
  return true;
}

inline bool isValidTransportToken(const String& token) {
  uint8_t bytes[32]{};
  return decodeTransportToken(token, bytes);
}

inline String transportHex(const uint8_t* bytes, size_t length) {
  static constexpr char kHex[] = "0123456789abcdef";
  String result;
  result.reserve(length * 2U);
  for (size_t index = 0; index < length; ++index) {
    result += kHex[(bytes[index] >> 4U) & 0x0fU];
    result += kHex[bytes[index] & 0x0fU];
  }
  return result;
}

inline bool transportHmac(const uint8_t* key, size_t keyLength, const String& input, uint8_t (&output)[32]) {
  const auto* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  return info != nullptr &&
         mbedtls_md_hmac(info, key, keyLength, reinterpret_cast<const unsigned char*>(input.c_str()), input.length(),
                         output) == 0;
}

inline String transportMac(const String& token, const char* direction, const String& input) {
  uint8_t root[32]{};
  uint8_t key[32]{};
  uint8_t mac[32]{};
  if (!decodeTransportToken(token, root) ||
      !transportHmac(root, sizeof(root), String("Inventatory Scan R1 transport v1 ") + direction, key) ||
      !transportHmac(key, sizeof(key), input, mac)) {
    return {};
  }
  return transportHex(mac, sizeof(mac));
}

inline String deviceRequestMac(const String& token, const char* method, const char* path, const String& deviceId,
                               uint64_t counter, const String& body) {
  return transportMac(token, "client-to-server", String("Inventatory Scan R1/v1\nrequest\n") + method + '\n' +
                                                 path + '\n' + deviceId + '\n' +
                                                 String(static_cast<unsigned long long>(counter)) + '\n' + body);
}

inline String deviceResponseMac(const String& token, uint64_t counter, int status, const String& body) {
  return transportMac(token, "server-to-client", String("Inventatory Scan R1/v1\nresponse\n") +
                                                 String(static_cast<unsigned long long>(counter)) +
                                                 '\n' + String(status) + '\n' + body);
}

inline bool transportMacMatches(const String& expected, const String& supplied) {
  if (expected.length() != 64 || expected.length() != supplied.length()) return false;
  uint8_t difference = 0;
  for (size_t index = 0; index < expected.length(); ++index) difference |= expected[index] ^ supplied[index];
  return difference == 0;
}

}  // namespace inventatory_scan
