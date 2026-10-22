// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstddef>

namespace inventatory_scan {

constexpr std::size_t kMaxHttpHeaderBytes = 1024U;
constexpr std::size_t kMaxHttpBodyBytes = 8192U;
constexpr std::size_t kMaxHttpResponseBytes = kMaxHttpHeaderBytes + 4U + kMaxHttpBodyBytes;

inline bool httpResponseAppendAllowed(std::size_t currentBytes) {
  return currentBytes < kMaxHttpResponseBytes;
}

inline bool httpHeaderWithinLimit(std::size_t headerBytes) {
  return headerBytes <= kMaxHttpHeaderBytes;
}

inline bool httpBodyWithinLimit(std::size_t bodyBytes) {
  return bodyBytes <= kMaxHttpBodyBytes;
}

// A name from an mDNS answer goes into the request's Host header. Any host on the network can answer,
// so only DNS name characters are allowed: a CR, LF or other byte could add header lines.
constexpr std::size_t kMaxHttpHostNameBytes = 253U;

inline bool isSafeHttpHostName(const char* name, std::size_t length) {
  if (name == nullptr || length == 0U || length > kMaxHttpHostNameBytes) return false;
  for (std::size_t index = 0; index < length; ++index) {
    const char ch = name[index];
    const bool allowed = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
                         ch == '-' || ch == '.';
    if (!allowed) return false;
  }
  return true;
}

}  // namespace inventatory_scan
