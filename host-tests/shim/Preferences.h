// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Host stand-in for the ESP32 Preferences (NVS) API. Namespaces persist in process memory,
// so a new object sees earlier writes, as a reset device sees flash. Tests inject write
// failures and power cuts with host_nvs::faults(), then call host_nvs::reboot().
#pragma once

#include "Arduino.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace host_nvs {

// NVS namespace and key names are limited to 15 characters plus the terminator.
constexpr std::size_t kMaxNameLength = 15;

// Approximate entry capacity; only bounds freeEntries().
constexpr std::size_t kEntryCapacity = 1000;

using Blob = std::vector<std::uint8_t>;
using Namespace = std::map<std::string, Blob>;

struct Faults {
  // Writes allowed before power cut (negative: never). Once spent, later writes and removes fail.
  long long writesBeforePowerCut = -1;
  // Fails every write and remove, like a full or broken partition.
  bool failWrites = false;
};

inline std::map<std::string, Namespace>& storage() {
  static std::map<std::string, Namespace> persisted;
  return persisted;
}

inline Faults& faults() {
  static Faults current;
  return current;
}

// Wipes storage and faults, as a freshly flashed device would start.
inline void reset() {
  storage().clear();
  faults() = Faults{};
}

// Clears the injected faults and keeps storage: the device rebooted.
inline void reboot() { faults() = Faults{}; }

// Consumes one write from the power budget. Returns false when the write must fail.
inline bool allowWrite() {
  Faults& current = faults();
  if (current.failWrites) return false;
  if (current.writesBeforePowerCut == 0) return false;
  if (current.writesBeforePowerCut > 0) --current.writesBeforePowerCut;
  return true;
}

inline bool validName(const char* name) {
  return name != nullptr && name[0] != '\0' && std::strlen(name) <= kMaxNameLength;
}

}  // namespace host_nvs

class Preferences {
 public:
  bool begin(const char* name, bool readOnly = false) {
    end();
    if (!host_nvs::validName(name)) return false;
    if (readOnly && host_nvs::storage().count(name) == 0) return false;
    name_ = name;
    readOnly_ = readOnly;
    open_ = true;
    return true;
  }

  void end() {
    open_ = false;
    name_.clear();
  }

  bool clear() {
    if (!writable() || !host_nvs::allowWrite()) return false;
    host_nvs::storage()[name_].clear();
    return true;
  }

  bool remove(const char* key) {
    if (!writable() || !validKey(key) || !host_nvs::allowWrite()) return false;
    return host_nvs::storage()[name_].erase(key) > 0;
  }

  bool isKey(const char* key) {
    return open_ && validKey(key) && find(key) != nullptr;
  }

  std::size_t freeEntries() {
    std::size_t used = 0;
    const auto space = host_nvs::storage().find(name_);
    if (space != host_nvs::storage().end()) used = space->second.size();
    return used >= host_nvs::kEntryCapacity ? 0 : host_nvs::kEntryCapacity - used;
  }

  std::size_t getBytesLength(const char* key) {
    const auto* blob = open_ && validKey(key) ? find(key) : nullptr;
    return blob != nullptr ? blob->size() : 0;
  }

  // Returns the number of bytes written, or 0 when the write failed.
  std::size_t putBytes(const char* key, const void* value, std::size_t length) {
    if (!writable() || !validKey(key) || value == nullptr || !host_nvs::allowWrite()) return 0;
    const auto* bytes = static_cast<const std::uint8_t*>(value);
    host_nvs::storage()[name_][key].assign(bytes, bytes + length);
    return length;
  }

  // Returns bytes read, or 0 if the key is missing or larger than maxLength.
  std::size_t getBytes(const char* key, void* buffer, std::size_t maxLength) {
    const auto* blob = open_ && validKey(key) ? find(key) : nullptr;
    if (blob == nullptr || blob->size() > maxLength || buffer == nullptr) return 0;
    if (!blob->empty()) std::memcpy(buffer, blob->data(), blob->size());
    return blob->size();
  }

  // Puts return bytes written (0 on failure); gets return the default if missing or wrong size.
  bool getBool(const char* key, bool defaultValue = false) {
    std::uint8_t value = 0;
    return readFixed(key, value) ? value != 0 : defaultValue;
  }
  std::size_t putBool(const char* key, bool value) {
    const std::uint8_t byte = value ? 1 : 0;
    return putBytes(key, &byte, sizeof(byte));
  }
  std::uint8_t getUChar(const char* key, std::uint8_t defaultValue = 0) {
    std::uint8_t value = 0;
    return readFixed(key, value) ? value : defaultValue;
  }
  std::size_t putUChar(const char* key, std::uint8_t value) { return putBytes(key, &value, sizeof(value)); }
  std::uint16_t getUShort(const char* key, std::uint16_t defaultValue = 0) {
    std::uint16_t value = 0;
    return readFixed(key, value) ? value : defaultValue;
  }
  std::size_t putUShort(const char* key, std::uint16_t value) { return putBytes(key, &value, sizeof(value)); }
  std::uint32_t getUInt(const char* key, std::uint32_t defaultValue = 0) {
    std::uint32_t value = 0;
    return readFixed(key, value) ? value : defaultValue;
  }
  std::size_t putUInt(const char* key, std::uint32_t value) { return putBytes(key, &value, sizeof(value)); }
  std::int32_t getInt(const char* key, std::int32_t defaultValue = 0) {
    std::int32_t value = 0;
    return readFixed(key, value) ? value : defaultValue;
  }
  std::size_t putInt(const char* key, std::int32_t value) { return putBytes(key, &value, sizeof(value)); }
  std::uint64_t getULong64(const char* key, std::uint64_t defaultValue = 0) {
    std::uint64_t value = 0;
    return readFixed(key, value) ? value : defaultValue;
  }
  std::size_t putULong64(const char* key, std::uint64_t value) { return putBytes(key, &value, sizeof(value)); }
  // Returns the number of characters stored, as the ESP32 library does, or 0 on failure.
  std::size_t putString(const char* key, const String& value) {
    const std::size_t length = value.length();
    return putBytes(key, value.c_str(), length + 1U) == length + 1U ? length : 0;
  }
  String getString(const char* key, const String& defaultValue = String()) {
    const auto* blob = open_ && validKey(key) ? find(key) : nullptr;
    if (blob == nullptr || blob->empty() || blob->back() != '\0') return defaultValue;
    return String(reinterpret_cast<const char*>(blob->data()));
  }

 private:
  template <typename T>
  bool readFixed(const char* key, T& value) {
    const auto* blob = open_ && validKey(key) ? find(key) : nullptr;
    if (blob == nullptr || blob->size() != sizeof(T)) return false;
    std::memcpy(&value, blob->data(), sizeof(T));
    return true;
  }

  static bool validKey(const char* key) {
    return key != nullptr && key[0] != '\0' && std::strlen(key) <= host_nvs::kMaxNameLength;
  }

  bool writable() const { return open_ && !readOnly_; }

  const host_nvs::Blob* find(const char* key) const {
    const auto space = host_nvs::storage().find(name_);
    if (space == host_nvs::storage().end()) return nullptr;
    const auto entry = space->second.find(key);
    return entry == space->second.end() ? nullptr : &entry->second;
  }

  std::string name_;
  bool open_ = false;
  bool readOnly_ = false;
};
