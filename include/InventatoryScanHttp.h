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

}  // namespace inventatory_scan
