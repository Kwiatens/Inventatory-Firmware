#pragma once

#include <cstddef>

namespace inventatory_scan {

constexpr std::size_t kMaxHttpHeaderBytes = 2048U;
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
